#pragma once

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/errors.hpp"

namespace transfer_fabric {

// Buffer allocation strategy (injected so the pool stays platform-neutral).
class TF_API BufferAllocator {
public:
    virtual ~BufferAllocator() = default;
    virtual void* alloc(std::size_t bytes) = 0;
    virtual void  free(void* ptr, std::size_t bytes) = 0;
    virtual std::string name() const = 0;
};

// Pageable host staging allocator (malloc-backed).
class TF_API HostAllocator : public BufferAllocator {
public:
    void* alloc(std::size_t bytes) override;
    void  free(void* ptr, std::size_t bytes) override;
    std::string name() const override { return "host"; }
};

// Pinned host staging allocator. When CUDA is enabled it uses cudaHostAlloc /
// cudaFreeHost. Otherwise it falls back to pageable memory and reports it so the
// runtime can be honest about capability.
class TF_API PinnedAllocator : public BufferAllocator {
public:
    PinnedAllocator();
    void* alloc(std::size_t bytes) override;
    void  free(void* ptr, std::size_t bytes) override;
    std::string name() const override { return "pinned"; }
    bool actually_pinned() const noexcept { return pinned_; }
private:
    bool pinned_{false};
};

// A staging buffer handed out to a transfer.
struct TF_API StagingBuffer {
    void*          data{nullptr};
    byte_count     size{0};
    std::uint64_t  token{0};
    bool           valid() const noexcept { return data != nullptr; }
};

// Bounded byte pool dedicated to transfer execution. It provides reuse (a free
// list keyed by exact size), strict total-byte bounds, wait/backpressure instead
// of unbounded allocation, high-water/reset telemetry, and clean shutdown.
class TF_API StagingPool {
public:
    StagingPool(PoolKind kind, std::shared_ptr<BufferAllocator> alloc,
                byte_count capacity_bytes, byte_count alignment);

    // Non-blocking allocate. Returns nullopt if the pool is exhausted and
    // cannot satisfy the request within its capacity (nowait=true).
    std::optional<StagingBuffer> allocate(byte_count bytes, bool nowait = false);

    // Blocking allocate: waits until capacity frees up (or cancellation).
    StagingBuffer allocate_blocking(byte_count bytes, const std::atomic<bool>& cancel);

    void release(StagingBuffer buf);

    PoolKind kind() const noexcept { return kind_; }
    byte_count capacity() const noexcept { return capacity_bytes_; }
    byte_count usage() const noexcept { return in_use_.load(std::memory_order_relaxed); }
    byte_count allocated_bytes() const noexcept { return allocated_bytes_; }
    byte_count high_water() const noexcept { return high_water_; }
    std::uint64_t reuse_count() const noexcept { return reuse_count_; }
    std::uint64_t total_allocs() const noexcept { return total_allocs_; }

    // Drain all in-use buffers and clear the free list. Used at shutdown after
    // all transfers are known to be retired.
    void drain();

    // Waits until the pool is completely idle (no in-use buffers).
    void wait_idle();

private:
    void adjust_high_water() noexcept;
    void* do_allocate(byte_count bytes);
    void  do_free(void* ptr, byte_count bytes);

    PoolKind kind_;
    std::shared_ptr<BufferAllocator> allocator_;
    byte_count capacity_bytes_;
    byte_count alignment_;
    std::atomic<byte_count> in_use_{0};
    byte_count allocated_bytes_{0};
    std::uint64_t high_water_{0};
    std::uint64_t reuse_count_{0};
    std::uint64_t total_allocs_{0};

    mutable std::mutex mu_;
    std::condition_variable cv_;
    // free list keyed by size, then vector of pointers
    std::vector<std::pair<byte_count, void*>> free_list_;
};

} // namespace transfer_fabric
