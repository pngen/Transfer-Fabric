#pragma once

#include <cstdint>
#include <string>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/endpoint.hpp"
#include "transfer_fabric/capabilities.hpp"
#include "transfer_fabric/integrity.hpp"

namespace transfer_fabric {

// The concrete backend names Transfer Fabric knows about.
namespace backend_name {
TF_API inline const char* host()  noexcept { return "host"; }
TF_API inline const char* cuda()  noexcept { return "cuda"; }
TF_API inline const char* file()  noexcept { return "file"; }
TF_API inline const char* shm()   noexcept { return "shared_memory"; }
TF_API inline const char* tcp()   noexcept { return "tcp"; }
} // namespace backend_name

// Everything a backend needs to perform one copy. For memory regions, src_ptr /
// dst_ptr are resolved addresses; for storage regions the object handle plus
// object offset are used. At most one side of a copy is "storage" for the
// backends implemented here (host/cuda move memory; file moves storage).
struct TF_API CopySpec {
    const void*  src_ptr{nullptr};
    void*        dst_ptr{nullptr};
    byte_count   bytes{0};
    byte_count   alignment{16};
    MemoryDomain src_domain{MemoryDomain::unknown};
    MemoryDomain dst_domain{MemoryDomain::unknown};
    device_id_t  src_device{0};
    device_id_t  dst_device{0};
    EndpointKind src_kind{EndpointKind::unknown};
    EndpointKind dst_kind{EndpointKind::unknown};
    // opaque storage object handles (file HANDLE, shm map handle)
    std::uint64_t src_object{0};
    std::uint64_t dst_object{0};
    byte_offset  src_obj_offset{0};
    byte_offset  dst_obj_offset{0};
};

// Handle to an in-flight asynchronous copy. Caller must wait() before reusing
// or freeing any resource referenced by the copy.
struct TF_API AsyncCopy {
    std::uint64_t token{0};
    std::string   backend;
};

// Abstract transfer backend. A backend owns no per-transfer state; it is shared
// and thread-safe. The runtime resolves endpoint handles into CopySpecs.
class TF_API Backend {
public:
    virtual ~Backend() = default;

    virtual std::string name() const = 0;
    virtual CapabilitySet capabilities() const = 0;

    // Eligibility: can this backend perform the given copy? Runtime must fail
    // explicitly (not silently fall back) if false.
    virtual bool can_copy(const CopySpec& spec) const = 0;

    // Synchronous copy.
    virtual Error copy(const CopySpec& spec) = 0;

    // Optional asynchronous copy. Default rejects; only backends that truly
    // support asynchrony override this.
    virtual bool supports_async() const noexcept { return false; }
    virtual Error submit(const CopySpec& spec, AsyncCopy& token) {
        (void)spec; (void)token;
        return Error(ErrorCategory::unsupported_path, name() + ": async copy unsupported");
    }
    virtual Error wait(AsyncCopy& token) {
        (void)token;
        return Error();
    }
};

// Optional interface for backends that support asynchronous, overlap-capable
// copies on an explicit stream (e.g. CUDA). Transfers detect this via dynamic
// cast and use it to build a real pipeline; otherwise they run synchronously.
class TF_API IStreamBackend {
public:
    virtual ~IStreamBackend() = default;
    virtual Error create_stream(void*& stream) = 0;
    virtual void destroy_stream(void* stream) = 0;
    virtual Error async_copy(const CopySpec& spec, void* stream) = 0;
    virtual Error sync(void* stream) = 0;
};

} // namespace transfer_fabric
