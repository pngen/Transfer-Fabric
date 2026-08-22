#include "transfer_fabric/staging.hpp"

#include <cstdlib>
#include <cstring>
#include <chrono>

#if defined(_WIN32)
#  include <malloc.h>
#endif

#if TF_ENABLE_CUDA
#  include <cuda_runtime.h>
#endif

namespace transfer_fabric {

// ---- HostAllocator ----------------------------------------------------
void* HostAllocator::alloc(std::size_t bytes) {
    if (bytes == 0) return nullptr;
#if defined(_WIN32)
    return _aligned_malloc(bytes, 64);
#else
    void* p = nullptr;
    if (posix_memalign(&p, 64, bytes) != 0) return nullptr;
    return p;
#endif
}
void HostAllocator::free(void* ptr, std::size_t) {
#if defined(_WIN32)
    _aligned_free(ptr);
#else
    std::free(ptr);
#endif
}

// ---- PinnedAllocator --------------------------------------------------
PinnedAllocator::PinnedAllocator() {
#if TF_ENABLE_CUDA
    pinned_ = true;
#else
    pinned_ = false;
#endif
}
void* PinnedAllocator::alloc(std::size_t bytes) {
    if (bytes == 0) return nullptr;
#if TF_ENABLE_CUDA
    void* p = nullptr;
    cudaError_t e = cudaMallocHost(&p, bytes);
    if (e != cudaSuccess) return nullptr;
    return p;
#else
    return std::malloc(bytes);
#endif
}
void PinnedAllocator::free(void* ptr, std::size_t bytes) {
#if TF_ENABLE_CUDA
    cudaError_t e = cudaFreeHost(ptr);
    (void)bytes; (void)e;
#else
    (void)bytes;
    std::free(ptr);
#endif
}

// ---- StagingPool ------------------------------------------------------
StagingPool::StagingPool(PoolKind kind, std::shared_ptr<BufferAllocator> alloc,
                         byte_count capacity_bytes, byte_count alignment)
    : kind_(kind), allocator_(std::move(alloc)),
      capacity_bytes_(capacity_bytes), alignment_(alignment ? alignment : 16) {}

void* StagingPool::do_allocate(byte_count bytes) {
    return allocator_->alloc(static_cast<std::size_t>(bytes));
}
void StagingPool::do_free(void* ptr, byte_count bytes) {
    allocator_->free(ptr, static_cast<std::size_t>(bytes));
}

void StagingPool::adjust_high_water() noexcept {
    if (allocated_bytes_ > high_water_) high_water_ = allocated_bytes_;
}

std::optional<StagingBuffer> StagingPool::allocate(byte_count bytes, bool nowait) {
    if (bytes == 0) return StagingBuffer{};
    std::lock_guard<std::mutex> g(mu_);
    // exact-size reuse
    for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
        if (it->first == bytes) {
            StagingBuffer b;
            b.data = it->second;
            b.size = bytes;
            b.token = 0;
            free_list_.erase(it);
            in_use_.fetch_add(bytes, std::memory_order_relaxed);
            ++reuse_count_;
            return b;
        }
    }
    // need new allocation, check capacity
    if (allocated_bytes_ + bytes > capacity_bytes_) {
        if (nowait) return std::nullopt;
        // caller uses allocate_blocking for waiting
        return std::nullopt;
    }
    void* p = do_allocate(bytes);
    if (p == nullptr) {
        // allocation failed (e.g. cudaHostAlloc failure / OOM)
        return std::nullopt;
    }
    allocated_bytes_ += bytes;
    adjust_high_water();
    ++total_allocs_;
    in_use_.fetch_add(bytes, std::memory_order_relaxed);
    StagingBuffer b;
    b.data = p;
    b.size = bytes;
    b.token = allocated_bytes_;
    // store token for debug/accounting
    return b;
}

StagingBuffer StagingPool::allocate_blocking(byte_count bytes, const std::atomic<bool>& cancel) {
    if (bytes == 0) return StagingBuffer{};
    std::unique_lock<std::mutex> l(mu_);
    while (true) {
        if (cancel.load(std::memory_order_relaxed)) {
            return StagingBuffer{}; // cancelled -> empty buffer signals abort
        }
        // exact-size reuse
        for (auto it = free_list_.begin(); it != free_list_.end(); ++it) {
            if (it->first == bytes) {
                StagingBuffer b;
                b.data = it->second;
                b.size = bytes;
                b.token = 0;
                free_list_.erase(it);
                in_use_.fetch_add(bytes, std::memory_order_relaxed);
                ++reuse_count_;
                return b;
            }
        }
        if (allocated_bytes_ + bytes <= capacity_bytes_) {
            void* p = do_allocate(bytes);
            if (p == nullptr) {
                // allocation failure is permanent for this request
                return StagingBuffer{}; // empty == failure; caller distinguishes
            }
            allocated_bytes_ += bytes;
            adjust_high_water();
            ++total_allocs_;
            in_use_.fetch_add(bytes, std::memory_order_relaxed);
            StagingBuffer b;
            b.data = p;
            b.size = bytes;
            b.token = allocated_bytes_;
            return b;
        }
        // Contention: backpressure. Wait for a release.
        cv_.wait_for(l, std::chrono::milliseconds(1));
    }
}

void StagingPool::release(StagingBuffer buf) {
    if (buf.data == nullptr || buf.size == 0) return;
    std::lock_guard<std::mutex> g(mu_);
    in_use_.fetch_sub(buf.size, std::memory_order_relaxed);
    free_list_.push_back({buf.size, buf.data});
    cv_.notify_all();
}

void StagingPool::drain() {
    std::lock_guard<std::mutex> g(mu_);
    for (auto& f : free_list_) {
        do_free(f.second, f.first);
    }
    free_list_.clear();
    allocated_bytes_ = 0;
    in_use_.store(0, std::memory_order_relaxed);
    cv_.notify_all();
}

void StagingPool::wait_idle() {
    std::unique_lock<std::mutex> l(mu_);
    while (in_use_.load(std::memory_order_relaxed) != 0) {
        cv_.wait(l);
    }
}

} // namespace transfer_fabric
