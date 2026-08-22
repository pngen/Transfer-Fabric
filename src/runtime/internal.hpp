#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/endpoint.hpp"
#include "transfer_fabric/handle.hpp"
#include "transfer_fabric/transfer.hpp"
#include "transfer_fabric/route.hpp"
#include "transfer_fabric/chunking.hpp"
#include "transfer_fabric/status.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/staging.hpp"
#include "transfer_fabric/telemetry.hpp"

namespace transfer_fabric {

// A resolved endpoint. Referenced by the transfer until it completes, so it may
// outlive an explicit unregister call (which only detaches it from the registry).
struct EndpointRecord {
    EndpointDescriptor desc;
    EndpointCapabilities caps;
    generation_t generation{0};
    // resolved resource handles
    std::uint64_t file_handle{0};   // file backend handle
    std::uint64_t shm_handle{0};    // mapping handle
    void*         shm_map{nullptr};
    byte_count    shm_len{0};

    ~EndpointRecord();   // closes file/shm handles if present
};

// The live transfer object. Internal only.
struct TransferRecord {
    TransferHandle handle;
    TransferOptions opts;
    Route route;
    ChunkPlan chunk_plan;
    TransferStatus status;
    std::mutex mtx;
    std::condition_variable cv;
    bool notified{false};
    std::atomic<bool> cancel{false};
    std::atomic<bool> started{false};
    std::atomic<int> deps_left{0};
    std::atomic<int> deps_failed{0};
    std::vector<TransferId> deps;
    // Pre-reserved staging buffers per intermediate node (indexed by node index).
    std::vector<std::vector<StagingBuffer>> staging_bufs;
    std::uint64_t created_ns{0};
    std::uint64_t queued_ns{0};
    std::uint64_t started_ns{0};
    std::uint64_t done_ns{0};

    std::shared_ptr<EndpointRecord> src_ep;
    std::shared_ptr<EndpointRecord> dst_ep;
};

// Slot + generation endpoint registry.
class EndpointRegistry {
public:
    struct Slot {
        std::shared_ptr<EndpointRecord> rec;
        generation_t gen{0};
        bool used{false};
    };

    EndpointRegistry();
    // Allocates a slot for a resolved endpoint. Returns handle + record.
    Error allocate(std::shared_ptr<EndpointRecord> rec, EndpointHandle& out);
    // Returns a copy of the record if the handle is valid and current.
    std::shared_ptr<EndpointRecord> get(EndpointHandle h) const;
    // Drops the slot if it is not referenced by any in-flight transfer.
    Error remove(EndpointHandle h);
    std::size_t size() const;
    void clear();

private:
    mutable std::mutex mu_;
    std::vector<Slot> slots_;
    std::vector<std::size_t> free_;
    std::size_t next_gen_{1};
};

// Transfer id -> record registry.
class TransferRegistry {
public:
    std::shared_ptr<TransferRecord> insert(const TransferId& id,
                                           std::shared_ptr<TransferRecord> rec);
    std::shared_ptr<TransferRecord> find(const TransferId& id) const;
    bool erase(const TransferId& id);
    std::size_t size() const;
    std::vector<std::shared_ptr<TransferRecord>> all() const;
    // count live objects that are not terminal and not a dependency-holder
    std::size_t live_count() const;
private:
    mutable std::mutex mu_;
    std::unordered_map<TransferId, std::shared_ptr<TransferRecord>> map_;
};

// Execution context handed to the executor.
struct RunContext {
    std::shared_ptr<TransferRecord> record;
    EndpointRegistry* endpoints{nullptr};
    std::unordered_map<std::string, std::unique_ptr<Backend>>* backends{nullptr};
    StagingPool* host_pool{nullptr};
    StagingPool* pinned_pool{nullptr};
    Telemetry* telemetry{nullptr};
    std::size_t pipeline_depth{2};
};

// Executes a single transfer to a terminal (possibly failed) state. Returns the
// terminal error (empty == success). Never throws.
Error run_transfer(RunContext& ctx);

// Helper: hash a region of an endpoint (host/device/file/shared/mmap) into a digest.
Error hash_endpoint_region(std::shared_ptr<EndpointRecord> ep, byte_offset offset,
                           byte_count len, VerificationMode mode,
                           std::array<std::uint8_t, 32>& digest_out);

} // namespace transfer_fabric
