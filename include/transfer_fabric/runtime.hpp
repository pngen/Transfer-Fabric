#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/handle.hpp"
#include "transfer_fabric/endpoint.hpp"
#include "transfer_fabric/transfer.hpp"
#include "transfer_fabric/status.hpp"
#include "transfer_fabric/chunking.hpp"
#include "transfer_fabric/route.hpp"
#include "transfer_fabric/planner.hpp"
#include "transfer_fabric/telemetry.hpp"
#include "transfer_fabric/capabilities.hpp"

namespace transfer_fabric {

// Full transfer description for inspection/reporting.
struct TF_API TransferInfo {
    TransferHandle     handle;
    TransferState      state{TransferState::created};
    Route              route;
    ChunkPlan          chunk_plan;
    byte_count         bytes{0};
    byte_count         completed{0};
    byte_count         verified{0};
    std::string        name_space;
    std::string        principal;
    EndpointHandle     source;
    EndpointHandle     destination;
    std::uint64_t      attempt{0};
    std::uint64_t      retries{0};
    Error              error;
    std::vector<TransferId> dependencies;
};

// The Transfer Fabric runtime. It owns the transfer registry, endpoint registry,
// planner, scheduler, staging pools, backends, and telemetry. It is the facade
// through which all transfer planning and execution happens.
class TF_API Runtime {
public:
    struct Config {
        std::size_t  worker_threads{4};
        byte_count   host_staging_capacity{256u * 1024u * 1024u};
        byte_count   pinned_staging_capacity{256u * 1024u * 1024u};
        std::size_t  max_queued_transfers{256};
        std::size_t  max_active_transfers{0};     // 0 -> worker_threads
        std::size_t  pipeline_depth{2};           // staging ring depth
        byte_count   default_chunk_size{64u * 1024u};
        std::string  name_space{"default"};
        bool         enable_cuda{true};
    };

    explicit Runtime(const Config& config);
    ~Runtime();
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;

    const Config& config() const noexcept { return config_; }

    // ---- Endpoints --------------------------------------------------
    Error register_endpoint(const EndpointDescriptor& desc, EndpointHandle& out);
    Error unregister_endpoint(EndpointHandle handle);
    Error query_endpoint(EndpointHandle handle, EndpointCapabilities& out) const;

    // ---- Planning (inspection) -------------------------------------
    // Plans a transfer between two registered endpoints without submitting it.
    PlanResult plan(const EndpointHandle src, const EndpointHandle dst,
                    byte_count bytes, const TransferPolicy& policy) const;
    // Lists the route for an options object, for the CLI 'plan' command.
    PlanResult plan(const TransferOptions& opts) const;

    // ---- Transfers --------------------------------------------------
    // Submits a transfer. Planning is synchronous; execution is asynchronous on
    // the scheduler. Returns the transfer handle. On planning/validation failure
    // returns an error and an invalid handle.
    TransferHandle submit(const TransferOptions& opts, Error& err);
    Error cancel(const TransferHandle& handle);
    TransferStatus status(const TransferHandle& handle) const;
    TransferInfo inspect(const TransferHandle& handle) const;
    // Blocks until the transfer reaches a terminal state. Returns false only on
    // cancellation/deadlock guard (not a workload timeout).
    bool wait(const TransferHandle& handle);

    // ---- Batching ---------------------------------------------------
    Error begin_batch(BatchHandle& out);
    Error end_batch(const BatchHandle& batch);

    // ---- Telemetry / introspection ---------------------------------
    TelemetrySnapshot telemetry() const;
    std::vector<std::string> available_backends() const;

    // ---- Shutdown ---------------------------------------------------
    // Drains in-flight work and releases every resource. Must be called before
    // the Runtime is destroyed; the destructor calls it if needed.
    void shutdown();
    bool is_shutting_down() const noexcept { return shutdown_.load(std::memory_order_relaxed); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    Config config_;
    std::atomic<bool> shutdown_{false};
};

} // namespace transfer_fabric
