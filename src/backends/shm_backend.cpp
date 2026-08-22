#include "transfer_fabric/backends/shm.hpp"

#include <cstring>

namespace transfer_fabric {

CapabilitySet SharedMemoryBackend::capabilities() const {
    CapabilitySet c;
    c.set(Capability::readable);
    c.set(Capability::writable);
    c.set(Capability::cpu_addressable);
    c.set(Capability::multiprocess);
    c.set(Capability::direct_copy);
    return c;
}

bool SharedMemoryBackend::can_copy(const CopySpec& spec) const {
    auto cpu = [](MemoryDomain d) {
        return d == MemoryDomain::host_pageable || d == MemoryDomain::host_pinned
            || d == MemoryDomain::shared || d == MemoryDomain::mmap;
    };
    bool any_shared = spec.src_domain == MemoryDomain::shared || spec.dst_domain == MemoryDomain::shared;
    return any_shared && cpu(spec.src_domain) && cpu(spec.dst_domain);
}

Error SharedMemoryBackend::copy(const CopySpec& spec) {
    if (spec.src_ptr == nullptr || spec.dst_ptr == nullptr) {
        return Error(ErrorCategory::invalid_request, "shared copy requires non-null pointers");
    }
    if (spec.bytes == 0) return Error();
    std::memcpy(spec.dst_ptr, spec.src_ptr, static_cast<std::size_t>(spec.bytes));
    return Error();
}

} // namespace transfer_fabric
