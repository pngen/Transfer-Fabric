#pragma once

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Host-callable CUDA primitives. Declared here so the host code (backends) can
// call them without including <cuda_runtime.h>; defined in cuda_backend.cu.
namespace platform::cuda {

struct TF_API DeviceInfo {
    int         ordinal{0};
    std::string name;
    std::uint64_t vram_bytes{0};
    int         compute_major{0};
    int         compute_minor{0};
    int         memory_clock_khz{0};
    int         memory_bus_width{0};
    int         pci_link_speed{0};
    bool        peer_supported{false};
};

// Returns the number of CUDA devices (0 if none / no driver).
TF_API int device_count() noexcept;
TF_API bool get_device_info(int ordinal, DeviceInfo& out) noexcept;
TF_API const char* error_string(int code) noexcept;

// Synchronous copy between host/device regions. chooes the cudaMemcpyKind from
// the domains.
TF_API Error device_copy(const void* src, void* dst, byte_count bytes,
                         MemoryDomain src_domain, MemoryDomain dst_domain,
                         int src_dev, int dst_dev) noexcept;

// Asynchronous, overlap-capable copy on a stream.
TF_API Error async_copy(const void* src, void* dst, byte_count bytes,
                        MemoryDomain src_domain, MemoryDomain dst_domain,
                        int src_dev, int dst_dev, void* stream) noexcept;

TF_API Error stream_create(void** stream) noexcept;
TF_API void stream_destroy(void* stream) noexcept;
TF_API Error stream_sync(void* stream) noexcept;

TF_API Error device_to_device(const void* src, void* dst, byte_count bytes,
                              int src_dev, int dst_dev) noexcept;

} // namespace platform::cuda
} // namespace transfer_fabric
