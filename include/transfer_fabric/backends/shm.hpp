#pragma once

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Shared-memory/multiprocess backend: moves bytes among CPU-addressable shared
// and host regions. It is used for process-local and cross-process shared-memory
// handoff. The actual copy is a synchronous memcpy over mapped views; the
// runtime creates/attaches the named shared regions at endpoint registration.
class TF_API SharedMemoryBackend : public Backend {
public:
    std::string name() const override { return backend_name::shm(); }
    CapabilitySet capabilities() const override;
    bool can_copy(const CopySpec& spec) const override;
    Error copy(const CopySpec& spec) override;
};

} // namespace transfer_fabric
