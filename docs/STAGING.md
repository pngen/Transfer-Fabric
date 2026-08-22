# Staging

Transfer Fabric owns two bounded staging pools: **host** and **pinned host**. They exist only to support transfer execution, not as a general allocator.

- Bounded total bytes, with a high-water mark.
- Exact-size reuse (free list) improves hit rate.
- Allocation failure returns `resource_exhausted`; the transfer fails or is deferred, never overallocates.
- `drain` releases all buffers at shutdown and `wait_idle` waits until idle.
- Pinned staging uses `cudaHostAlloc` when CUDA is available; otherwise it falls back to pageable and reports honestly.
