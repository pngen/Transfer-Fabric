#pragma once

#include <cstdint>
#include <string>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/capabilities.hpp"

namespace transfer_fabric {

// Concrete endpoint kind. This maps to a backend that knows how to address it.
enum class TF_API EndpointKind : std::uint8_t {
    unknown      = 0,
    host,          // pageable/pinned host memory
    device,        // accelerator device memory
    file,          // regular file / storage region
    shared,        // process-shared memory region
    mmap,          // memory-mapped file region
    remote,        // logical remote endpoint (transported)
};

TF_API inline const char* to_string(EndpointKind k) noexcept {
    switch (k) {
        case EndpointKind::unknown: return "unknown";
        case EndpointKind::host:    return "host";
        case EndpointKind::device:  return "device";
        case EndpointKind::file:    return "file";
        case EndpointKind::shared:  return "shared";
        case EndpointKind::mmap:    return "mmap";
        case EndpointKind::remote:  return "remote";
    }
    return "unknown";
}

// A value describing the capture point of an externally owned resource. Transfer
// Fabric never allocates the underlying memory; it only references it through
// these descriptors. Offsets/lengths are validated before any dereference.
struct TF_API EndpointDescriptor {
    EndpointKind kind{EndpointKind::unknown};
    MemoryDomain domain{MemoryDomain::unknown};

    // Memory-like endpoints.
    void*        base{nullptr};
    byte_count   size{0};

    // File / storage / shared endpoints.
    byte_offset  offset{0};
    std::string  path;          // file path, shared-memory name, or mmap path

    // Device endpoints.
    device_id_t  device_id{0};

    // Remote endpoints.
    std::string  remote_host;
    std::uint16_t remote_port{0};

    // Whether a pointer refers to pinned (locked) host memory.
    bool         pinned{false};

    // Factory helpers ------------------------------------------------
    static EndpointDescriptor host_memory(void* base, byte_count size, bool pinned = false) {
        EndpointDescriptor d;
        d.kind = EndpointKind::host;
        d.domain = pinned ? MemoryDomain::host_pinned : MemoryDomain::host_pageable;
        d.base = base; d.size = size; d.pinned = pinned;
        return d;
    }
    static EndpointDescriptor device_memory(device_id_t device, void* base, byte_count size) {
        EndpointDescriptor d;
        d.kind = EndpointKind::device;
        d.domain = MemoryDomain::device;
        d.device_id = device; d.base = base; d.size = size;
        return d;
    }
    static EndpointDescriptor file_region(std::string path, byte_offset offset, byte_count size) {
        EndpointDescriptor d;
        d.kind = EndpointKind::file; d.domain = MemoryDomain::storage;
        d.path = std::move(path); d.offset = offset; d.size = size;
        return d;
    }
    static EndpointDescriptor shared_region(std::string name, byte_offset offset, byte_count size) {
        EndpointDescriptor d;
        d.kind = EndpointKind::shared; d.domain = MemoryDomain::shared;
        d.path = std::move(name); d.offset = offset; d.size = size;
        return d;
    }
    static EndpointDescriptor mmap_region(std::string path, byte_offset offset, byte_count size) {
        EndpointDescriptor d;
        d.kind = EndpointKind::mmap; d.domain = MemoryDomain::mmap;
        d.path = std::move(path); d.offset = offset; d.size = size;
        return d;
    }
    static EndpointDescriptor remote(std::string host, std::uint16_t port) {
        EndpointDescriptor d;
        d.kind = EndpointKind::remote; d.domain = MemoryDomain::remote;
        d.remote_host = std::move(host); d.remote_port = port;
        return d;
    }

    bool valid() const noexcept { return kind != EndpointKind::unknown; }
};

} // namespace transfer_fabric
