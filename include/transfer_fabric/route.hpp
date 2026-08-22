#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/endpoint.hpp"
#include "transfer_fabric/capabilities.hpp"
#include "transfer_fabric/backends/backend.hpp"

namespace transfer_fabric {

// Route classification.
enum class TF_API RouteClass : std::uint8_t {
    unknown = 0,
    direct,    // single leg, no staging
    staged,    // at least one staging leg
    remote,    // uses a remote/network transport leg
};

TF_API inline const char* to_string(RouteClass c) noexcept {
    switch (c) {
        case RouteClass::direct: return "direct";
        case RouteClass::staged: return "staged";
        case RouteClass::remote: return "remote";
        case RouteClass::unknown: return "unknown";
    }
    return "unknown";
}

// A single movement leg: a (source -> destination) step with known capability
// and cost. Legs are produced by the planner, not invented by callers.
struct TF_API Leg {
    EndpointKind source_kind{EndpointKind::unknown};
    EndpointKind dest_kind{EndpointKind::unknown};
    MemoryDomain source_domain{MemoryDomain::unknown};
    MemoryDomain dest_domain{MemoryDomain::unknown};
    std::string  backend;

    byte_count   staging_bytes{0};
    byte_count   preferred_chunk{64u * 1024u};
    byte_count   max_chunk{256u * 1024u * 1024u};
    byte_count   alignment{16};

    double       estimated_bandwidth{1.0};   // bytes / second
    double       fixed_setup{0.0};           // seconds
    CapabilitySet caps;

    bool direct{false};
    bool staged{false};
    bool async{false};
    bool overlap{false};
    bool supports_verification{false};

    friend constexpr bool operator==(const Leg&, const Leg&) = default;
};

// The cost of a route under the current cost model.
struct TF_API CostEstimate {
    double total{0.0};
    double fixed_setup{0.0};
    double bandwidth_time{0.0};
    double staging_penalty{0.0};
    double hop_penalty{0.0};
    double queue_penalty{0.0};
    double verification_penalty{0.0};
    double retry_risk{0.0};
};

// The selected movement plan: an ordered sequence of legs plus aggregate cost.
struct TF_API Route {
    std::vector<Leg> legs;
    CostEstimate cost;
    RouteClass route_class{RouteClass::unknown};
    std::uint64_t hop_count{0};
    byte_count staging_bytes{0};
    std::string route_id;         // stable, deterministic id derived from structure
    std::string description;      // e.g. "file -> host_pinned -> device (staged)"

    bool is_direct() const noexcept { return route_class == RouteClass::direct; }
    bool involves_remote() const noexcept { return route_class == RouteClass::remote; }
    bool empty() const noexcept { return legs.empty(); }
};

} // namespace transfer_fabric
