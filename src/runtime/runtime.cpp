#include "transfer_fabric/runtime.hpp"
#include "internal.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "transfer_fabric/backends/host.hpp"
#include "transfer_fabric/backends/file.hpp"
#include "transfer_fabric/backends/shm.hpp"
#include "transfer_fabric/backends/cuda.hpp"
#include "transfer_fabric/platform/cuda_detect.hpp"
#include "transfer_fabric/scheduler.hpp"
#include "transfer_fabric/staging.hpp"

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace transfer_fabric {

EndpointRecord::~EndpointRecord() {
#if defined(_WIN32)
    if (file_handle) { CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(file_handle))); file_handle = 0; }
    if (shm_handle) { CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(shm_handle))); shm_handle = 0; }
#endif
}

using clock_type = std::chrono::steady_clock;
std::uint64_t now_ns() { return std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count(); }

namespace {
bool has_parent_component(const std::string& path) {
    std::filesystem::path p(path);
    for (const auto& comp : p) {
        if (comp == ".." || comp == ".") return true;
    }
    return false;
}

EndpointCapabilities caps_for_kind(const EndpointDescriptor& desc) {
    EndpointCapabilities c;
    c.memory_domain = desc.domain;
    c.device_id = desc.device_id;
    c.required_alignment = 16;
    c.preferred_transfer_size = 64u * 1024u;
    c.max_transfer_size = desc.size;
    switch (desc.kind) {
        case EndpointKind::host:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::cpu_addressable); c.capabilities.set(Capability::direct_copy);
            c.capabilities.set(Capability::staged_copy);
            break;
        case EndpointKind::device:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::accelerator_addressable); c.capabilities.set(Capability::direct_copy);
            c.capabilities.set(Capability::staged_copy); c.capabilities.set(Capability::async_copy);
            c.capabilities.set(Capability::overlap); c.capabilities.set(Capability::peer_copy);
            c.capabilities.set(Capability::verification); c.capabilities.set(Capability::cpu_addressable);
            c.preferred_transfer_size = 8u << 20;
            break;
        case EndpointKind::file:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::persistent); c.capabilities.set(Capability::direct_copy);
            c.capabilities.set(Capability::staged_copy);
            break;
        case EndpointKind::shared:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::cpu_addressable); c.capabilities.set(Capability::multiprocess);
            c.capabilities.set(Capability::direct_copy);
            break;
        case EndpointKind::mmap:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::cpu_addressable); c.capabilities.set(Capability::direct_copy);
            break;
        case EndpointKind::remote:
            c.capabilities.set(Capability::readable); c.capabilities.set(Capability::writable);
            c.capabilities.set(Capability::multiprocess); c.capabilities.set(Capability::direct_copy);
            break;
        case EndpointKind::unknown:
            break;
    }
    c.is_local = desc.kind != EndpointKind::remote;
    return c;
}
} // namespace

// ========================================================================
// Runtime::Impl
// ========================================================================
struct Runtime::Impl {
    Config cfg;
    EndpointRegistry endpoints;
    TransferRegistry transfers;
    std::unique_ptr<Scheduler> scheduler;
    std::unique_ptr<DefaultRoutePlanner> planner;
    std::unique_ptr<StagingPool> host_pool;
    std::unique_ptr<StagingPool> pinned_pool;
    std::unordered_map<std::string, std::unique_ptr<Backend>> backends;
    Telemetry telemetry;
    mutable std::mutex dep_mtx;
    std::unordered_map<TransferId, std::vector<TransferId>> dep_waiters;
    std::mutex batch_mtx;
    std::vector<std::pair<generation_t, std::uint64_t>> batch_slots; // gen, active count
    std::vector<std::size_t> batch_free;

    explicit Impl(const Config& c) : cfg(c) {
        host_pool = std::make_unique<StagingPool>(PoolKind::host,
            std::make_shared<HostAllocator>(), cfg.host_staging_capacity, 64);
        pinned_pool = std::make_unique<StagingPool>(PoolKind::host_pinned,
            std::make_shared<PinnedAllocator>(), cfg.pinned_staging_capacity, 64);
        backends[backend_name::host()] = std::make_unique<HostBackend>();
        backends[backend_name::file()] = std::make_unique<FileBackend>();
        backends[backend_name::shm()] = std::make_unique<SharedMemoryBackend>();
#if TF_ENABLE_CUDA
        if (cfg.enable_cuda && platform::cuda::device_count() > 0) {
            backends[backend_name::cuda()] = std::make_unique<CudaBackend>();
        }
#endif
        planner = std::make_unique<DefaultRoutePlanner>();
        std::size_t workers = cfg.worker_threads ? cfg.worker_threads : 4;
        std::size_t maxq = cfg.max_queued_transfers ? cfg.max_queued_transfers : 1;
        scheduler = std::make_unique<Scheduler>(workers, maxq);
        scheduler->start();
    }

    std::vector<std::string> backend_names() const {
        std::vector<std::string> v;
        for (auto& [k, b] : backends) v.push_back(k);
        return v;
    }

    // ---- helpers ---------------------------------------------------
    void set_state(const std::shared_ptr<TransferRecord>& rec, TransferState s) {
        std::lock_guard<std::mutex> g(rec->mtx);
        if (!StateTransitionTable::allowed(rec->status.state, s)) {
            // In debug builds, be strict. In release, ignore invalid (shouldn't happen).
            return;
        }
        rec->status.state = s;
        switch (s) {
            case TransferState::queued: rec->queued_ns = now_ns(); break;
            case TransferState::active: rec->started_ns = now_ns(); break;
            default: break;
        }
    }

    Error reserve_staging(const std::shared_ptr<TransferRecord>& rec) {
        const auto& route = rec->route;
        if (route.legs.empty()) return Error();
        std::vector<MemoryDomain> nodes;
        nodes.push_back(route.legs[0].source_domain);
        for (const auto& L : route.legs) nodes.push_back(L.dest_domain);
        const std::size_t nnodes = nodes.size();
        rec->staging_bufs.assign(nnodes, {});
        const std::size_t depth = cfg.pipeline_depth > 0 ? cfg.pipeline_depth : 2;
        for (std::size_t j = 1; j + 1 < nnodes; ++j) {
            StagingPool* pool = (nodes[j] == MemoryDomain::host_pinned) ? pinned_pool.get() : host_pool.get();
            std::vector<StagingBuffer>& bufs = rec->staging_bufs[j];
            for (std::size_t k = 0; k < depth; ++k) {
                auto b = pool->allocate(rec->chunk_plan.chunk_size(), /*nowait=*/true);
                if (!b) {
                    // release already allocated in this node and previous nodes
                    for (auto& bb : rec->staging_bufs[j]) pool->release(bb);
                    for (std::size_t m = 1; m < j; ++m) {
                        StagingPool* pp = (nodes[m] == MemoryDomain::host_pinned) ? pinned_pool.get() : host_pool.get();
                        for (auto& bb : rec->staging_bufs[m]) pp->release(bb);
                    }
                    rec->staging_bufs.clear();
                    return Error(ErrorCategory::resource_exhausted, "staging reservation failed");
                }
                bufs.push_back(*b);
                telemetry.add_bytes_staged(rec->chunk_plan.chunk_size());
                telemetry.staging_pool_hit(true);
            }
        }
        return Error();
    }

    void release_staging(const std::shared_ptr<TransferRecord>& rec) {
        const auto& route = rec->route;
        if (route.legs.empty()) return;
        std::vector<MemoryDomain> nodes;
        nodes.push_back(route.legs[0].source_domain);
        for (const auto& L : route.legs) nodes.push_back(L.dest_domain);
        for (std::size_t j = 1; j + 1 < nodes.size(); ++j) {
            StagingPool* pool = (nodes[j] == MemoryDomain::host_pinned) ? pinned_pool.get() : host_pool.get();
            for (auto& b : rec->staging_bufs.size() > j ? rec->staging_bufs[j] : std::vector<StagingBuffer>{}) {
                pool->release(b);
            }
        }
        rec->staging_bufs.clear();
    }

    void set_terminal(const std::shared_ptr<TransferRecord>& rec, TransferState s, const Error& e) {
        std::lock_guard<std::mutex> g(rec->mtx);
        if (is_terminal(rec->status.state)) return;
        rec->status.state = s;
        rec->status.error = e;
        rec->done_ns = now_ns();
        if (!rec->notified) { rec->notified = true; }
        rec->cv.notify_all();
    }

    void finalize(const std::shared_ptr<TransferRecord>& rec, const Error& e) {
        TransferState terminal;
        if (e.ok()) terminal = TransferState::completed;
        else if (e.category == ErrorCategory::cancellation) terminal = TransferState::cancelled;
        else terminal = TransferState::failed;

        set_terminal(rec, terminal, e);
        if (rec->started.exchange(false, std::memory_order_relaxed)) telemetry.transfer_done();

        if (e.ok()) {
            telemetry.transfer_completed();
            telemetry.add_bytes_moved(rec->status.bytes_completed);
            if (rec->route.is_direct()) telemetry.add_direct_path_bytes(rec->status.bytes_completed);
            else telemetry.add_staged_path_bytes(rec->status.bytes_completed);
            for (const auto& L : rec->route.legs) telemetry.add_backend_bytes(L.backend, rec->status.bytes_completed);
            telemetry.add_route_bytes(rec->route.route_id, rec->status.bytes_completed);
            telemetry.add_route_class_bytes(rec->route.route_class, rec->status.bytes_completed);
            if (rec->status.bytes_verified) telemetry.add_bytes_verified(rec->status.bytes_verified);
        } else if (e.category == ErrorCategory::cancellation) {
            telemetry.transfer_cancelled();
            telemetry.cancellation();
        } else {
            telemetry.transfer_failed();
            switch (e.category) {
                case ErrorCategory::integrity_failure: telemetry.integrity_failure(); break;
                case ErrorCategory::backend_failure: telemetry.backend_failure(); break;
                case ErrorCategory::transient_transport: telemetry.transport_failure(); break;
                default: break;
            }
        }

        release_staging(rec);
        resolve_dependents(rec->handle.id, e.ok());
    }

    void resolve_dependents(const TransferId& completed, bool success) {
        std::vector<std::shared_ptr<TransferRecord>> ready;
        std::vector<std::shared_ptr<TransferRecord>> fail;
        {
            std::lock_guard<std::mutex> g(dep_mtx);
            auto it = dep_waiters.find(completed);
            if (it == dep_waiters.end()) return;
            for (const TransferId& depid : it->second) {
                auto rec = transfers.find(depid);
                if (!rec) continue;
                rec->deps_left--;
                if (!success) rec->deps_failed++;
                if (rec->deps_left.load() == 0) {
                    if (rec->deps_failed.load() == 0) ready.push_back(rec);
                    else fail.push_back(rec);
                }
            }
            dep_waiters.erase(it);
        }
        for (auto& r : fail) {
            finalize(r, Error(ErrorCategory::permanent_failure, "a dependency failed"));
        }
        for (auto& r : ready) {
            queue_transfer(r);
        }
    }

    void run_driver(const std::shared_ptr<TransferRecord>& rec) {
        set_state(rec, TransferState::active);
        rec->started.store(true, std::memory_order_relaxed);
        telemetry.transfer_started();
        telemetry.queue_popped();
        RunContext ctx;
        ctx.record = rec;
        ctx.endpoints = &endpoints;
        ctx.backends = &backends;
        ctx.host_pool = host_pool.get();
        ctx.pinned_pool = pinned_pool.get();
        ctx.telemetry = &telemetry;
        ctx.pipeline_depth = cfg.pipeline_depth;
        std::uint64_t t0 = now_ns();
        Error result = run_transfer(ctx);
        std::uint64_t t1 = now_ns();
        telemetry.add_execution_time(t1 - t0);
        finalize(rec, result);
    }

    void queue_transfer(const std::shared_ptr<TransferRecord>& rec) {
        Error reserve = reserve_staging(rec);
        if (!reserve.ok()) {
            finalize(rec, reserve);
            return;
        }
        set_state(rec, TransferState::reserved);
        set_state(rec, TransferState::queued);
        telemetry.transfer_queued();
        telemetry.queue_pushed();
        Scheduler::Task task;
        task.body = [this, rec]() { run_driver(rec); };
        task.priority = rec->opts.priority;
        task.name = "transfer-";
        if (!scheduler->submit(std::move(task))) {
            telemetry.queue_popped();
            finalize(rec, Error(ErrorCategory::resource_exhausted, "scheduler queue full"));
        }
    }

    // Synchronous execution path: runs the transfer inline and finalizes it
    // before the caller proceeds. This guarantees a correct, deadlock-free core.
    // It still pipelines chunks within a single transfer (async CUDA copies on
    // streams overlap with host staging reads); it does not overlap transfers.
    void execute_sync(const std::shared_ptr<TransferRecord>& rec) {
        Error reserve = reserve_staging(rec);
        if (!reserve.ok()) { finalize(rec, reserve); return; }
        set_state(rec, TransferState::reserved);
        set_state(rec, TransferState::queued);
        telemetry.transfer_queued();
        set_state(rec, TransferState::active);
        rec->started.store(true, std::memory_order_relaxed);
        telemetry.transfer_started();
        RunContext ctx;
        ctx.record = rec;
        ctx.endpoints = &endpoints;
        ctx.backends = &backends;
        ctx.host_pool = host_pool.get();
        ctx.pinned_pool = pinned_pool.get();
        ctx.telemetry = &telemetry;
        ctx.pipeline_depth = cfg.pipeline_depth;
        std::uint64_t t0 = now_ns();
        Error result = run_transfer(ctx);
        telemetry.add_execution_time(now_ns() - t0);
        finalize(rec, result);
    }
};

// ========================================================================
// Runtime
// ========================================================================
Runtime::Runtime(const Config& config) : config_(config), impl_(std::make_unique<Impl>(config)) {}
Runtime::~Runtime() { shutdown(); }


Error Runtime::register_endpoint(const EndpointDescriptor& desc, EndpointHandle& out) {
    if (shutdown_.load()) { out = {}; return Error(ErrorCategory::invalid_request, "runtime shutting down"); }
    if (!desc.valid()) { out = {}; return Error(ErrorCategory::invalid_request, "invalid endpoint descriptor"); }

    if (desc.kind == EndpointKind::file && has_parent_component(desc.path)) {
        out = {}; return Error(ErrorCategory::permission_denied, "path traversal in file path");
    }

    auto rec = std::make_shared<EndpointRecord>();
    rec->desc = desc;
    rec->caps = caps_for_kind(desc);
    rec->generation = 0;

#if defined(_WIN32)
    if (desc.kind == EndpointKind::file) {
        HANDLE h = CreateFileA(desc.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h == INVALID_HANDLE_VALUE) {
            out = {}; return Error(ErrorCategory::backend_failure, "cannot open file endpoint");
        }
        LARGE_INTEGER sz; sz.QuadPart = 0;
        if (!GetFileSizeEx(h, &sz)) { CloseHandle(h); out = {}; return Error(ErrorCategory::backend_failure, "cannot stat file"); }
        rec->file_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(h));
        rec->caps.max_transfer_size = static_cast<byte_count>(sz.QuadPart) - desc.offset;
    } else if (desc.kind == EndpointKind::shared) {
        HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                         0, static_cast<DWORD>(desc.size), desc.path.c_str());
        if (hMap == NULL) { out = {}; return Error(ErrorCategory::backend_failure, "cannot create shared mapping"); }
        void* p = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, desc.size);
        if (!p) { CloseHandle(hMap); out = {}; return Error(ErrorCategory::backend_failure, "cannot map shared memory"); }
        rec->shm_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hMap));
        rec->shm_map = p; rec->shm_len = desc.size;
        rec->desc.base = p; // resolved address
    } else if (desc.kind == EndpointKind::mmap) {
        HANDLE hFile = CreateFileA(desc.path.c_str(), GENERIC_READ | GENERIC_WRITE,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                   OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile == INVALID_HANDLE_VALUE) { out = {}; return Error(ErrorCategory::backend_failure, "cannot open mmap file"); }
        HANDLE hMap = CreateFileMappingA(hFile, nullptr, PAGE_READWRITE, 0, 0, nullptr);
        if (hMap == NULL) { CloseHandle(hFile); out = {}; return Error(ErrorCategory::backend_failure, "cannot map file"); }
        void* p = MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, 0);
        if (!p) { CloseHandle(hMap); CloseHandle(hFile); out = {}; return Error(ErrorCategory::backend_failure, "cannot map view"); }
        rec->file_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hFile));
        rec->shm_handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(hMap));
        rec->shm_map = p;
        rec->desc.base = p;
    }
#endif

    if (desc.kind == EndpointKind::device) {
        if (!platform::cuda::device_count() || desc.device_id >= static_cast<device_id_t>(platform::cuda::device_count())) {
            out = {}; return Error(ErrorCategory::unsupported_path, "invalid CUDA device");
        }
    }
    if (desc.kind == EndpointKind::host) {
        if (desc.base == nullptr) { out = {}; return Error(ErrorCategory::invalid_request, "host endpoint needs a non-null base"); }
    }
    if (desc.kind == EndpointKind::device && desc.base == nullptr) {
        out = {}; return Error(ErrorCategory::invalid_request, "device endpoint needs a non-null base pointer");
    }

    return impl_->endpoints.allocate(rec, out);
}

Error Runtime::unregister_endpoint(EndpointHandle handle) {
    return impl_->endpoints.remove(handle);
}

Error Runtime::query_endpoint(EndpointHandle handle, EndpointCapabilities& out) const {
    auto ep = impl_->endpoints.get(handle);
    if (!ep) return Error(ErrorCategory::stale_generation, "invalid endpoint handle");
    out = ep->caps;
    return Error();
}

PlanResult Runtime::plan(const EndpointHandle src, const EndpointHandle dst,
                         byte_count bytes, const TransferPolicy& policy) const {
    auto se = impl_->endpoints.get(src);
    auto de = impl_->endpoints.get(dst);
    PlanResult res;
    if (!se || !de) { res.reason = "invalid endpoint handle"; return res; }
    if (!se->caps.capabilities.has(Capability::readable)) { res.reason = "source not readable"; return res; }
    if (!de->caps.capabilities.has(Capability::writable)) { res.reason = "destination not writable"; return res; }
    PlannerInput in;
    in.src = se->caps; in.dst = de->caps;
    in.bytes = bytes; in.policy = policy;
    in.host_staging_capacity = impl_->host_pool->capacity();
    in.pinned_staging_capacity = impl_->pinned_pool->capacity();
    in.remote_available = false;
    in.available_backends = impl_->backend_names();
    return impl_->planner->plan(in);
}

PlanResult Runtime::plan(const TransferOptions& opts) const {
    return plan(opts.source, opts.destination, opts.source_range.length, opts.policy);
}

Error resolve_ranges(const std::shared_ptr<EndpointRecord>& se, const std::shared_ptr<EndpointRecord>& de,
                     ByteRange& srcRange, ByteRange& dstRange, byte_count& bytes) {
    byte_count srcsize = se->caps.max_transfer_size;
    byte_count dstsize = de->caps.max_transfer_size;
    if (srcRange.empty()) srcRange = ByteRange{0, srcsize};
    if (srcRange.offset > srcsize || srcRange.length > (srcsize - srcRange.offset)) {
        return Error(ErrorCategory::invalid_request, "source range out of bounds");
    }
    bytes = srcRange.length;
    if (dstRange.empty()) dstRange = ByteRange{0, bytes};
    if (dstRange.length < bytes) {
        return Error(ErrorCategory::invalid_request, "destination range too small");
    }
    if (dstRange.offset > dstsize || bytes > (dstsize - dstRange.offset)) {
        return Error(ErrorCategory::invalid_request, "destination range out of bounds");
    }
    return Error();
}

TransferHandle Runtime::submit(const TransferOptions& opts, Error& err) {
    TransferHandle out;
    err = Error();
    if (shutdown_.load()) { err = Error(ErrorCategory::invalid_request, "runtime shutting down"); return out; }
    if (!opts.source.valid() || !opts.destination.valid()) { err = Error(ErrorCategory::invalid_request, "invalid endpoint handle"); return out; }
    auto se = impl_->endpoints.get(opts.source);
    auto de = impl_->endpoints.get(opts.destination);
    if (!se || !de) { err = Error(ErrorCategory::stale_generation, "invalid endpoint handle"); return out; }

    // Resolve ranges.
    ByteRange srcRange = opts.source_range;
    ByteRange dstRange = opts.destination_range;
    byte_count bytes = 0;
    err = resolve_ranges(se, de, srcRange, dstRange, bytes);
    if (!err.ok()) return out;

    // Plan.
    PlannerInput pin;
    pin.src = se->caps; pin.dst = de->caps;
    pin.bytes = bytes; pin.policy = opts.policy;
    pin.host_staging_capacity = impl_->host_pool->capacity();
    pin.pinned_staging_capacity = impl_->pinned_pool->capacity();
    pin.remote_available = false;
    pin.available_backends = impl_->backend_names();
    PlanResult pr = impl_->planner->plan(pin);
    if (!pr.found) { err = Error(ErrorCategory::unsupported_path, pr.reason.empty() ? "no route" : pr.reason); return out; }

    // Chunk plan from the most constrained leg.
    byte_count preferred = opts.policy.preferred_chunk_size;
    for (const auto& L : pr.best.legs) if (L.preferred_chunk < preferred) preferred = L.preferred_chunk;
    byte_count maxc = opts.policy.max_chunk_size;
    for (const auto& L : pr.best.legs) if (L.max_chunk < maxc) maxc = L.max_chunk;
    byte_count minc = 4096;
    byte_count align = 16;
    for (const auto& L : pr.best.legs) if (L.alignment > align) align = L.alignment;
    ChunkPlan cp;
    try {
        cp = ChunkPlan::build(ChunkPlanInput{bytes, srcRange.offset, dstRange.offset, minc, maxc, preferred, align});
    } catch (...) { err = Error(ErrorCategory::invalid_request, "chunk plan failed"); return out; }

    auto rec = std::make_shared<TransferRecord>();
    rec->handle.id = opts.explicit_id.valid() ? opts.explicit_id.id : TransferId::generate();
    rec->opts = opts;
    rec->route = pr.best;
    rec->chunk_plan = cp;
    rec->created_ns = now_ns();
    rec->status.state = TransferState::created;
    rec->status.bytes_scheduled = bytes;
    rec->status.chunk_count = cp.chunk_count();
    rec->src_ep = se;
    rec->dst_ep = de;
    rec->deps = opts.dependencies;
    rec->deps_left.store(static_cast<int>(opts.dependencies.size()));
    rec->deps_failed.store(0);
    rec->status.attempt = 1;

    impl_->transfers.insert(rec->handle.id, rec);
    impl_->telemetry.transfer_created();
    impl_->telemetry.add_bytes_requested(bytes);
    impl_->telemetry.add_chunks(cp.chunk_count());
    impl_->set_state(rec, TransferState::planned);

    if (!opts.dependencies.empty()) {
        // Synchronous dependency resolution: wait for each dependency to reach a
        // terminal state; if any failed, this transfer fails before executing.
        for (const TransferId& d : opts.dependencies) {
            auto dep = impl_->transfers.find(d);
            if (!dep) { err = Error(ErrorCategory::invalid_request, "unknown dependency"); return out; }
            bool dcomp = false;
            {
                std::unique_lock<std::mutex> l(dep->mtx);
                dep->cv.wait(l, [&] { return dep->notified && is_terminal(dep->status.state); });
                dcomp = dep->status.state == TransferState::completed;
            }
            if (!dcomp) {
                err = Error(ErrorCategory::permanent_failure, "a dependency failed");
                impl_->set_terminal(rec, TransferState::failed, err);
                return out;
            }
        }
        impl_->queue_transfer(rec);
    } else {
        impl_->queue_transfer(rec);
    }
    out = rec->handle;
    return out;
}

Error Runtime::cancel(const TransferHandle& handle) {
    auto rec = impl_->transfers.find(handle.id);
    if (!rec) return Error(ErrorCategory::stale_generation, "unknown transfer");
    {
        std::lock_guard<std::mutex> g(rec->mtx);
        if (is_terminal(rec->status.state)) return Error(ErrorCategory::invalid_request, "transfer already terminal");
    }
    rec->cancel.store(true);
    return Error();
}

TransferStatus Runtime::status(const TransferHandle& handle) const {
    TransferStatus s;
    auto rec = impl_->transfers.find(handle.id);
    if (!rec) { s.state = TransferState::failed; s.error = Error(ErrorCategory::stale_generation, "unknown transfer"); return s; }
    std::lock_guard<std::mutex> g(rec->mtx);
    s = rec->status;
    return s;
}

TransferInfo Runtime::inspect(const TransferHandle& handle) const {
    TransferInfo info;
    auto rec = impl_->transfers.find(handle.id);
    if (!rec) { info.handle = handle; info.state = TransferState::failed; info.error = Error(ErrorCategory::stale_generation, "unknown transfer"); return info; }
    std::lock_guard<std::mutex> g(rec->mtx);
    info.handle = rec->handle;
    info.state = rec->status.state;
    info.route = rec->route;
    info.chunk_plan = rec->chunk_plan;
    info.bytes = rec->status.bytes_scheduled;
    info.completed = rec->status.bytes_completed;
    info.verified = rec->status.bytes_verified;
    info.name_space = rec->opts.name_space;
    info.principal = rec->opts.principal;
    info.source = rec->opts.source;
    info.destination = rec->opts.destination;
    info.attempt = rec->status.attempt;
    info.retries = rec->status.retries;
    info.error = rec->status.error;
    info.dependencies = rec->deps;
    return info;
}

bool Runtime::wait(const TransferHandle& handle) {
    auto rec = impl_->transfers.find(handle.id);
    if (!rec) return false;
    std::unique_lock<std::mutex> l(rec->mtx);
    rec->cv.wait(l, [&] { return rec->notified && is_terminal(rec->status.state); });
    return rec->status.state == TransferState::completed;
}

Error Runtime::begin_batch(BatchHandle& out) {
    std::lock_guard<std::mutex> g(impl_->batch_mtx);
    std::size_t index;
    if (!impl_->batch_free.empty()) { index = impl_->batch_free.back(); impl_->batch_free.pop_back(); }
    else { index = impl_->batch_slots.size(); impl_->batch_slots.emplace_back(); }
    // generation
    // use index+1 slot, gen increments
    impl_->batch_slots[index].second = 0;
    out.slot = index + 1;
    out.generation = index + 1; // simple monotonic per slot for now
    impl_->telemetry.add_batch();
    return Error();
}

Error Runtime::end_batch(const BatchHandle& batch) {
    std::lock_guard<std::mutex> g(impl_->batch_mtx);
    if (batch.slot == 0 || batch.slot > impl_->batch_slots.size()) return Error(ErrorCategory::invalid_request, "bad batch");
    impl_->batch_slots[batch.slot - 1].second = 0;
    return Error();
}

TelemetrySnapshot Runtime::telemetry() const { return impl_->telemetry.snapshot(); }
std::vector<std::string> Runtime::available_backends() const { return impl_->backend_names(); }

void Runtime::shutdown() {
    if (shutdown_.exchange(true)) return;
    impl_->scheduler->shutdown();
    impl_->host_pool->drain();
    impl_->pinned_pool->drain();
    impl_->endpoints.clear();
}

} // namespace transfer_fabric
