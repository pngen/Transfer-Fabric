#include "transfer_fabric/platform/cuda.hpp"

#include <cuda_runtime.h>
#include <cstring>

namespace transfer_fabric::platform::cuda {

int device_count() noexcept {
    int n = 0;
    cudaError_t e = cudaGetDeviceCount(&n);
    if (e != cudaSuccess) return 0;
    return n;
}

const char* error_string(int code) noexcept {
    const char* s = cudaGetErrorString(static_cast<cudaError_t>(code));
    return s ? s : "unknown cuda error";
}

bool get_device_info(int ordinal, DeviceInfo& out) noexcept {
    cudaDeviceProp p;
    cudaError_t e = cudaGetDeviceProperties(&p, ordinal);
    if (e != cudaSuccess) return false;
    out.ordinal = ordinal;
    out.name = p.name;
    out.vram_bytes = static_cast<std::uint64_t>(p.totalGlobalMem);
    out.compute_major = p.major;
    out.compute_minor = p.minor;
    out.memory_clock_khz = 0;
    out.memory_bus_width = 0;
    out.pci_link_speed = 0;
    out.peer_supported = false;
    return true;
}

namespace {
cudaMemcpyKind kind_for(MemoryDomain src, MemoryDomain dst) noexcept {
    bool src_dev = src == MemoryDomain::device;
    bool dst_dev = dst == MemoryDomain::device;
    if (src_dev && dst_dev) return cudaMemcpyDeviceToDevice;
    if (src_dev && !dst_dev) return cudaMemcpyDeviceToHost;
    if (!src_dev && dst_dev) return cudaMemcpyHostToDevice;
    return cudaMemcpyHostToHost;
}
Error make_error(cudaError_t e, const char* what) noexcept {
    if (e == cudaSuccess) return Error();
    return Error(ErrorCategory::backend_failure, static_cast<std::uint64_t>(e),
                 std::string(what) + ": " + cudaGetErrorString(e));
}
} // namespace

Error device_copy(const void* src, void* dst, byte_count bytes,
                  MemoryDomain src_domain, MemoryDomain dst_domain,
                  int src_dev, int dst_dev) noexcept {
    (void)src_dev; (void)dst_dev;
    if (bytes == 0) return Error();
    if (src == nullptr || dst == nullptr) {
        return Error(ErrorCategory::invalid_request, "cuda copy: null pointer");
    }
    cudaMemcpyKind kind = kind_for(src_domain, dst_domain);
    cudaError_t e = cudaMemcpy(dst, src, static_cast<std::size_t>(bytes), kind);
    if (e != cudaSuccess) return make_error(e, "cudaMemcpy");
    e = cudaGetLastError();
    if (e != cudaSuccess) return make_error(e, "cudaMemcpy");
    return Error();
}

Error async_copy(const void* src, void* dst, byte_count bytes,
                 MemoryDomain src_domain, MemoryDomain dst_domain,
                 int src_dev, int dst_dev, void* stream) noexcept {
    (void)src_dev; (void)dst_dev;
    if (bytes == 0) return Error();
    if (src == nullptr || dst == nullptr) {
        return Error(ErrorCategory::invalid_request, "cuda async copy: null pointer");
    }
    cudaMemcpyKind kind = kind_for(src_domain, dst_domain);
    cudaStream_t s = static_cast<cudaStream_t>(stream);
    cudaError_t e = cudaMemcpyAsync(dst, src, static_cast<std::size_t>(bytes), kind, s);
    if (e != cudaSuccess) return make_error(e, "cudaMemcpyAsync");
    return Error();
}

Error stream_create(void** stream) noexcept {
    cudaStream_t s;
    cudaError_t e = cudaStreamCreate(&s);
    if (e != cudaSuccess) return make_error(e, "cudaStreamCreate");
    *stream = s;
    return Error();
}
void stream_destroy(void* stream) noexcept {
    if (!stream) return;
    cudaStreamSynchronize(static_cast<cudaStream_t>(stream));
    cudaStreamDestroy(static_cast<cudaStream_t>(stream));
}
Error stream_sync(void* stream) noexcept {
    cudaError_t e = cudaStreamSynchronize(static_cast<cudaStream_t>(stream));
    if (e != cudaSuccess) return make_error(e, "cudaStreamSynchronize");
    return Error();
}

Error device_to_device(const void* src, void* dst, byte_count bytes,
                       int src_dev, int dst_dev) noexcept {
    (void)src_dev; (void)dst_dev;
    cudaError_t e = cudaMemcpy(const_cast<void*>(dst), src,
                               static_cast<std::size_t>(bytes),
                               cudaMemcpyDeviceToDevice);
    if (e != cudaSuccess) return make_error(e, "cudaMemcpyDeviceToDevice");
    return Error();
}

} // namespace transfer_fabric::platform::cuda
