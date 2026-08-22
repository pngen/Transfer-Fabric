#pragma once

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/backends/backend.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// File/storage backend: reads and writes file region destinations/regions.
// It handles file<->host and file<->file copies with bounded chunks and strict
// short-read/short-write rejection.
class TF_API FileBackend : public Backend {
public:
    std::string name() const override { return backend_name::file(); }
    CapabilitySet capabilities() const override;
    bool can_copy(const CopySpec& spec) const override;
    Error copy(const CopySpec& spec) override;
};

} // namespace transfer_fabric
