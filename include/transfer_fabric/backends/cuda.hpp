#pragma once

#include <mutex>
#include <unordered_map>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/platform/cuda.hpp"

namespace transfer_fabric {

// Real CUDA backend. It executes pageable/pinned host <-> device and device <->
// device copies using the CUDA runtime. It supports genuine asynchronous,
// overlap-capable copies through the IStreamBackend interface.
class TF_API CudaBackend : public Backend, public IStreamBackend {
public:
    std::string name() const override { return backend_name::cuda(); }
    CapabilitySet capabilities() const override;
    bool can_copy(const CopySpec& spec) const override;
    Error copy(const CopySpec& spec) override;

    bool supports_async() const noexcept override { return true; }
    Error submit(const CopySpec& spec, AsyncCopy& token) override;
    Error wait(AsyncCopy& token) override;

    // IStreamBackend
    Error create_stream(void*& stream) override;
    void destroy_stream(void* stream) override;
    Error async_copy(const CopySpec& spec, void* stream) override;
    Error sync(void* stream) override;

private:
    std::mutex mu_;
    std::unordered_map<std::uint64_t, void*> active_;
    std::uint64_t next_token_{1};
};

} // namespace transfer_fabric
