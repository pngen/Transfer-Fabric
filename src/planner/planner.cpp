#include "transfer_fabric/planner.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

namespace transfer_fabric {

// ---------------------------------------------------------------------------
// Cost model
// ---------------------------------------------------------------------------
CostEstimate CostModel::evaluate(const std::vector<Leg>& legs, byte_count bytes,
                                 const RouteWeights& w) noexcept {
    CostEstimate c;
    if (legs.empty()) return c;
    double setup = 0.0, bw_time = 0.0, staging = 0.0, hop = 0.0;
    double retry = 0.0;
    byte_count staged_bytes = 0;
    for (std::size_t i = 0; i < legs.size(); ++i) {
        const Leg& L = legs[i];
        setup += L.fixed_setup;
        double bw = L.estimated_bandwidth > 0.0 ? L.estimated_bandwidth : 1.0;
        bw_time += static_cast<double>(bytes) / bw;
        hop += 1.0;
        if (L.staged || (i > 0 && i + 1 < legs.size())) {
            staging += 1.0;
            staged_bytes += bytes;
        }
        // risk from taking a bandwidth-limited hop
        retry += L.caps.has(Capability::staged_copy) ? 0.05 : 0.0;
    }
    c.fixed_setup = setup * w.fixed_setup;
    c.bandwidth_time = bw_time * w.bandwidth;
    c.staging_penalty = static_cast<double>(staged_bytes) / (1e9 + bytes) * w.staging;
    c.hop_penalty = hop * w.hop;
    c.retry_risk = retry * w.retry_risk;
    double num_hops = static_cast<double>(legs.size());
    c.total = c.fixed_setup + c.bandwidth_time + c.staging_penalty + c.hop_penalty +
              c.retry_risk;
    (void)num_hops;
    return c;
}

CostEstimate CostModel::evaluate(const Route& route, byte_count bytes,
                                 const RouteWeights& w) noexcept {
    return evaluate(route.legs, bytes, w);
}

// ---------------------------------------------------------------------------
// Domain graph
// ---------------------------------------------------------------------------
namespace {
struct EdgeDef {
    MemoryDomain a, b;
    const char* backend;
    double       bw;    // bytes/sec
    double       setup; // seconds
    bool         remote;
};

EndpointKind kind_of_domain(MemoryDomain d) noexcept {
    switch (d) {
        case MemoryDomain::host_pageable: return EndpointKind::host;
        case MemoryDomain::host_pinned:   return EndpointKind::host;
        case MemoryDomain::device:        return EndpointKind::device;
        case MemoryDomain::shared:        return EndpointKind::shared;
        case MemoryDomain::storage:       return EndpointKind::file;
        case MemoryDomain::mmap:          return EndpointKind::mmap;
        case MemoryDomain::remote:        return EndpointKind::remote;
        case MemoryDomain::unknown:       return EndpointKind::unknown;
    }
    return EndpointKind::unknown;
}

const std::vector<EdgeDef>& edges() {
    static const std::vector<EdgeDef> e = {
        {MemoryDomain::host_pageable, MemoryDomain::host_pageable, backend_name::host(), 9.0e9, 0.00002, false},
        {MemoryDomain::host_pageable, MemoryDomain::host_pinned,   backend_name::host(), 8.0e9, 0.00002, false},
        {MemoryDomain::host_pinned,   MemoryDomain::host_pinned,   backend_name::host(), 9.0e9, 0.00002, false},
        {MemoryDomain::host_pageable, MemoryDomain::device,        backend_name::cuda(), 6.0e9, 0.00005, false},
        {MemoryDomain::host_pinned,   MemoryDomain::device,        backend_name::cuda(), 2.2e10,0.00003, false},
        {MemoryDomain::device,        MemoryDomain::device,        backend_name::cuda(), 3.0e10,0.00002, false},
        {MemoryDomain::host_pageable, MemoryDomain::storage,       backend_name::file(), 2.0e9, 0.00005, false},
        {MemoryDomain::host_pinned,   MemoryDomain::storage,       backend_name::file(), 2.0e9, 0.00005, false},
        {MemoryDomain::storage,       MemoryDomain::storage,       backend_name::file(), 2.0e9, 0.00003, false},
        {MemoryDomain::host_pageable, MemoryDomain::shared,        backend_name::shm(),  1.0e10,0.00003, false},
        {MemoryDomain::host_pinned,   MemoryDomain::shared,        backend_name::shm(),  1.0e10,0.00003, false},
        {MemoryDomain::shared,        MemoryDomain::shared,        backend_name::shm(),  1.0e10,0.00003, false},
        {MemoryDomain::host_pageable, MemoryDomain::mmap,          backend_name::host(), 8.0e9, 0.00003, false},
        {MemoryDomain::host_pinned,   MemoryDomain::mmap,          backend_name::host(), 8.0e9, 0.00003, false},
        {MemoryDomain::host_pageable, MemoryDomain::remote,        backend_name::tcp(),  1.0e9, 0.00015, true},
        {MemoryDomain::host_pinned,   MemoryDomain::remote,        backend_name::tcp(),  1.0e9, 0.00015, true},
    };
    return e;
}

bool backend_known(const std::string& b, const std::vector<std::string>& avail) {
    if (avail.empty()) return true;
    return std::find(avail.begin(), avail.end(), b) != avail.end();
}

// Does a domain represent a staging intermediate?
bool is_staging_domain(MemoryDomain d) noexcept {
    return d == MemoryDomain::host_pageable || d == MemoryDomain::host_pinned;
}

bool feasible_edge(const EdgeDef& ed, const PlannerInput& in) {
    if (ed.remote && !in.remote_available) return false;
    if (!backend_known(ed.backend, in.available_backends)) return false;
    return true;
}
} // namespace

std::string DefaultRoutePlanner::describe(const std::vector<Leg>& legs) {
    std::string out;
    for (std::size_t i = 0; i < legs.size(); ++i) {
        if (!out.empty()) out += " -> ";
        out += to_string(legs[i].source_domain);
        out += ">";
        out += to_string(legs[i].dest_domain);
    }
    return out;
}

std::string DefaultRoutePlanner::route_id(const std::vector<Leg>& legs) {
    std::string out;
    for (std::size_t i = 0; i < legs.size(); ++i) {
        if (!out.empty()) out += "|";
        out += to_string(legs[i].source_domain);
        out += "-";
        out += to_string(legs[i].dest_domain);
        out += ":";
        out += legs[i].backend;
    }
    return out;
}

PlanResult DefaultRoutePlanner::plan(const PlannerInput& in) const {
    PlanResult res;
    if (!in.src.capabilities.has(Capability::readable)) {
        res.reason = "source is not readable";
        return res;
    }
    if (!in.dst.capabilities.has(Capability::writable)) {
        res.reason = "destination is not writable";
        return res;
    }
    if (!in.policy.domain_allowed(in.src.memory_domain) ||
        !in.policy.domain_allowed(in.dst.memory_domain)) {
        res.reason = "domain not permitted by policy";
        return res;
    }
    if (in.bytes == 0) {
        // Zero-byte transfer: a valid single direct leg with no work.
        Leg z;
        z.source_kind = kind_of_domain(in.src.memory_domain);
        z.dest_kind = kind_of_domain(in.dst.memory_domain);
        z.source_domain = in.src.memory_domain;
        z.dest_domain = in.dst.memory_domain;
        z.backend = backend_name::host();
        z.caps = in.src.capabilities;
        z.direct = true;
        res.found = true;
        res.best.legs = {z};
        res.best.route_class = RouteClass::direct;
        res.best.hop_count = 1;
        res.best.route_id = route_id(res.best.legs);
        res.best.description = describe(res.best.legs);
        res.candidates.push_back(res.best);
        return res;
    }

    // Depth-first simple-path enumeration over the small domain graph.
    const auto& eg = edges();
    const MemoryDomain start = in.src.memory_domain;
    const MemoryDomain goal = in.dst.memory_domain;

    std::vector<Route> candidates;

    // Same-domain transfers (host to host, device to device, file to file, shared
    // to shared) are direct single-leg moves. Build that leg directly.
    if (start == goal) {
        const char* backend = nullptr;
        switch (start) {
            case MemoryDomain::device:  backend = backend_name::cuda(); break;
            case MemoryDomain::storage: backend = backend_name::file(); break;
            case MemoryDomain::shared:  backend = backend_name::shm();  break;
            case MemoryDomain::mmap:    backend = backend_name::host(); break;
            default:                    backend = backend_name::host(); break;
        }
        if (backend_known(backend, in.available_backends)) {
            Leg L;
            L.source_kind = kind_of_domain(start);
            L.dest_kind = kind_of_domain(goal);
            L.source_domain = start;
            L.dest_domain = goal;
            L.backend = backend;
            L.caps = in.src.capabilities;
            L.direct = true;
            if (backend == backend_name::cuda()) L.estimated_bandwidth = 3.0e10;
            else if (backend == backend_name::file()) L.estimated_bandwidth = 2.0e9;
            else if (backend == backend_name::shm()) L.estimated_bandwidth = 1.0e10;
            else L.estimated_bandwidth = 9.0e9;
            L.fixed_setup = 0.00002;
            Route r;
            r.legs.push_back(L);
            r.route_class = RouteClass::direct;
            r.hop_count = 1;
            r.staging_bytes = 0;
            r.route_id = route_id(r.legs);
            r.description = describe(r.legs);
            r.cost = CostModel::evaluate(r, in.bytes, in.policy.weights);
            candidates.push_back(std::move(r));
        }
        if (candidates.empty()) {
            res.reason = "no direct same-domain route";
            return res;
        }
        res.found = true;
        res.best = candidates.front();
        res.candidates = std::move(candidates);
        return res;
    }
    std::vector<MemoryDomain> path{start};
    std::unordered_set<MemoryDomain> visited{start};

    std::function<void(MemoryDomain)> dfs = [&](MemoryDomain cur) {
        if (path.size() - 1 > in.policy.max_hops) return;
        if (cur == goal && path.size() > 1) {
            // Build a route from the path.
            Route r;
            byte_count est_chunk = in.policy.preferred_chunk_size;
            bool direct_ok = (path.size() == 2);
            r.route_class = direct_ok ? RouteClass::direct : RouteClass::staged;
            bool remote = false;
            byte_count staging_req = 0;
            bool feasible = true;
            std::size_t staging_nodes = 0;
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                MemoryDomain a = path[i], b = path[i+1];
                // find the edge/reverse edge
                const EdgeDef* ed = nullptr;
                for (const auto& e : eg) {
                    if ((e.a == a && e.b == b) || (e.a == b && e.b == a)) { ed = &e; break; }
                }
                if (!ed) { feasible = false; break; }
                if (!backend_known(ed->backend, in.available_backends)) { feasible = false; break; }
                if (std::string(ed->backend) == backend_name::tcp() && !in.remote_available) { feasible = false; break; }
                if (ed->remote) remote = true;
                Leg L;
                L.source_kind = kind_of_domain(a);
                L.dest_kind = kind_of_domain(b);
                L.source_domain = a;
                L.dest_domain = b;
                L.backend = ed->backend;
                L.estimated_bandwidth = ed->bw;
                L.fixed_setup = ed->setup;
                if (a == in.src.memory_domain) {
                    L.caps = in.src.capabilities;
                    if (!in.src.capabilities.has(Capability::readable)) { feasible = false; break; }
                }
                if (b == in.dst.memory_domain) {
                    L.caps = in.dst.capabilities;
                    if (!in.dst.capabilities.has(Capability::writable)) { feasible = false; break; }
                }
                // staging markers
                bool src_staging = is_staging_domain(a) && a != start;
                bool dst_staging = is_staging_domain(b) && b != goal;
                L.staged = src_staging || dst_staging;
                L.direct = L.staged ? false : true;
                r.legs.push_back(L);
                if (dst_staging && b != goal) {
                    ++staging_nodes;
                    staging_req += est_chunk > 0 ? est_chunk : 4096;
                }
                if (src_staging && a != start) { ++staging_nodes; }
            }
            if (!feasible) return;
            // host_pageable staging requires the pageable pool; host_pinned requires pinned.
            byte_count pageable_need = 0, pinned_need = 0;
            for (std::size_t i = 0; i + 1 < path.size(); ++i) {
                MemoryDomain a = path[i], b = path[i+1];
                if (b != goal && b == MemoryDomain::host_pinned) pinned_need += est_chunk;
                if (b != goal && b == MemoryDomain::host_pageable) pageable_need += est_chunk;
                if (a != start && a == MemoryDomain::host_pinned) pinned_need += est_chunk;
                if (a != start && a == MemoryDomain::host_pageable) pageable_need += est_chunk;
            }
            // x2 double-buffer
            pageable_need *= 2; pinned_need *= 2;
            if (pinned_need > in.pinned_staging_capacity ||
                pageable_need > in.host_staging_capacity) return;

            if (in.policy.require_staging && r.route_class != RouteClass::staged) return;
            if (!in.policy.allow_staging && r.route_class == RouteClass::staged) return;
            if (in.policy.prefer_direct && r.route_class == RouteClass::staged) {
                // Only allow staged if no direct route exists (checked below by ranking).
            }
            r.hop_count = r.legs.size();
            r.staging_bytes = staging_req;
            r.route_id = route_id(r.legs);
            r.description = describe(r.legs);
            r.cost = CostModel::evaluate(r, in.bytes, in.policy.weights);
            if (remote) r.route_class = RouteClass::remote;
            candidates.push_back(std::move(r));
            return;
        }
        for (const auto& e : eg) {
            MemoryDomain nxt;
            if (e.a == cur) nxt = e.b;
            else if (e.b == cur) nxt = e.a;
            else continue;
            if (visited.count(nxt)) continue;
            if (!feasible_edge(e, in)) continue;
            visited.insert(nxt);
            path.push_back(nxt);
            dfs(nxt);
            path.pop_back();
            visited.erase(nxt);
        }
    };
    dfs(start);

    // Rank by cost. If prefer_direct and a direct route exists and is feasible,
    // prefer it unless a staged route is strictly cheaper in a meaningful way.
    std::sort(candidates.begin(), candidates.end(),
              [](const Route& a, const Route& b) { return a.cost.total < b.cost.total; });

    // Filter: prefer direct when policy says so and a direct route is nearly as good.
    if (in.policy.prefer_direct) {
        auto has_direct = std::any_of(candidates.begin(), candidates.end(),
                                      [](const Route& r) { return r.is_direct(); });
        if (has_direct) {
            for (auto& c : candidates) {
                if (!c.is_direct()) { c.cost.total += 0.5; }
            }
            std::sort(candidates.begin(), candidates.end(),
                      [](const Route& a, const Route& b) { return a.cost.total < b.cost.total; });
        }
    }

    if (candidates.empty()) {
        res.reason = "no feasible route";
        return res;
    }
    res.found = true;
    res.best = candidates.front();
    res.candidates = std::move(candidates);
    return res;
}

} // namespace transfer_fabric
