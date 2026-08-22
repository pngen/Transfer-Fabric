#include "transfer_fabric/backends/host.hpp"

#include <cstring>

namespace transfer_fabric {

CapabilitySet HostBackend::capabilities() const {
    CapabilitySet c;
    c.set(Capability::readable);
    c.set(Capability::writable);
    c.set(Capability::cpu_addressable);
    c.set(Capability::direct_copy);
    return c;
}

bool HostBackend::can_copy(const CopySpec& spec) const {
    // Host backend moves bytes among pageable/pinned host and memory-mapped
    // regions. Shared-memory regions use the dedicated SharedMemoryBackend.
    auto cpu = [](MemoryDomain d) {
        return d == MemoryDomain::host_pageable || d == MemoryDomain::host_pinned
            || d == MemoryDomain::mmap;
    };
    return cpu(spec.src_domain) && cpu(spec.dst_domain);
}

Error HostBackend::copy(const CopySpec& spec) {
    if (spec.src_ptr == nullptr || spec.dst_ptr == nullptr) {
        return Error(ErrorCategory::invalid_request, "host copy requires non-null pointers");
    }
    if (spec.bytes == 0) return Error();
    std::memcpy(spec.dst_ptr, spec.src_ptr, static_cast<std::size_t>(spec.bytes));
    return Error();
}

} // namespace transfer_fabric
