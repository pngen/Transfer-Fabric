#include "transfer_fabric/backends/cuda.hpp"

namespace transfer_fabric {

CapabilitySet CudaBackend::capabilities() const {
    CapabilitySet c;
    c.set(Capability::readable);
    c.set(Capability::writable);
    c.set(Capability::accelerator_addressable);
    c.set(Capability::cpu_addressable);
    c.set(Capability::direct_copy);
    c.set(Capability::staged_copy);
    c.set(Capability::async_copy);
    c.set(Capability::overlap);
    c.set(Capability::verification);
    return c;
}

bool CudaBackend::can_copy(const CopySpec& spec) const {
    bool src_host = spec.src_domain == MemoryDomain::host_pageable
                 || spec.src_domain == MemoryDomain::host_pinned;
    bool dst_host = spec.dst_domain == MemoryDomain::host_pageable
                 || spec.dst_domain == MemoryDomain::host_pinned;
    bool src_dev = spec.src_domain == MemoryDomain::device;
    bool dst_dev = spec.dst_domain == MemoryDomain::device;
    return (src_host && dst_dev) || (src_dev && dst_host) || (src_dev && dst_dev);
}

Error CudaBackend::copy(const CopySpec& spec) {
    if (!can_copy(spec)) {
        return Error(ErrorCategory::unsupported_path, "cuda can_copy=false");
    }
    return platform::cuda::device_copy(spec.src_ptr, spec.dst_ptr, spec.bytes,
                                       spec.src_domain, spec.dst_domain,
                                       static_cast<int>(spec.src_device),
                                       static_cast<int>(spec.dst_device));
}

Error CudaBackend::submit(const CopySpec& spec, AsyncCopy& token) {
    if (!can_copy(spec)) {
        return Error(ErrorCategory::unsupported_path, "cuda async can_copy=false");
    }
    void* stream = nullptr;
    Error e = platform::cuda::stream_create(&stream);
    if (!e.ok()) return e;
    e = platform::cuda::async_copy(spec.src_ptr, spec.dst_ptr, spec.bytes,
                                   spec.src_domain, spec.dst_domain,
                                   static_cast<int>(spec.src_device),
                                   static_cast<int>(spec.dst_device), stream);
    if (!e.ok()) { platform::cuda::stream_destroy(stream); return e; }
    std::lock_guard<std::mutex> g(mu_);
    std::uint64_t tok = next_token_++;
    active_[tok] = stream;
    token.token = tok;
    token.backend = name();
    return Error();
}

Error CudaBackend::wait(AsyncCopy& token) {
    if (token.token == 0) return Error();
    void* stream = nullptr;
    {
        std::lock_guard<std::mutex> g(mu_);
        auto it = active_.find(token.token);
        if (it == active_.end()) return Error(ErrorCategory::permanent_failure, "unknown async token");
        stream = it->second;
        active_.erase(it);
    }
    Error e = platform::cuda::stream_sync(stream);
    platform::cuda::stream_destroy(stream);
    token.token = 0;
    return e;
}

Error CudaBackend::create_stream(void*& stream) {
    return platform::cuda::stream_create(&stream);
}
void CudaBackend::destroy_stream(void* stream) {
    if (stream) platform::cuda::stream_destroy(stream);
}
Error CudaBackend::async_copy(const CopySpec& spec, void* stream) {
    return platform::cuda::async_copy(spec.src_ptr, spec.dst_ptr, spec.bytes,
                                      spec.src_domain, spec.dst_domain,
                                      static_cast<int>(spec.src_device),
                                      static_cast<int>(spec.dst_device), stream);
}
Error CudaBackend::sync(void* stream) {
    return platform::cuda::stream_sync(stream);
}

} // namespace transfer_fabric
