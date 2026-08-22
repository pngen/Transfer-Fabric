#pragma once

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Host backend: moves bytes between any two CPU-addressable regions (pageable
// host, pinned host, shared memory, and memory-mapped files) via memcpy. It is
// synchronous and never performs accelerator access.
class TF_API HostBackend : public Backend {
public:
    std::string name() const override { return backend_name::host(); }
    CapabilitySet capabilities() const override;
    bool can_copy(const CopySpec& spec) const override;
    Error copy(const CopySpec& spec) override;
};

} // namespace transfer_fabric
