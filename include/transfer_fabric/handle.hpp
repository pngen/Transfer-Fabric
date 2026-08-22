#pragma once

#include <cstdint>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/transfer_id.hpp"

namespace transfer_fabric {

// Endpoints and batches are held in generation-aware slot registries: a handle
// is only valid if both its slot and generation still refer to a live object.
// Slot is 1-based; slot 0 is always invalid. This makes stale/forged handles
// deterministically rejectable without any global state.
struct TF_API EndpointHandle {
    std::uint64_t slot{0};
    generation_t  generation{0};

    bool valid() const noexcept { return slot != 0; }
    friend constexpr bool operator==(const EndpointHandle&, const EndpointHandle&) = default;
};

struct TF_API BatchHandle {
    std::uint64_t slot{0};
    generation_t  generation{0};

    bool valid() const noexcept { return slot != 0; }
    friend constexpr bool operator==(const BatchHandle&, const BatchHandle&) = default;
};

// A transfer is identified by its unique 128-bit id. Because those ids are never
// reused, no extra generation is required; the record keeps an attempt counter.
struct TF_API TransferHandle {
    TransferId id{};

    bool valid() const noexcept { return id.valid(); }
    friend constexpr bool operator==(const TransferHandle&, const TransferHandle&) = default;
};

} // namespace transfer_fabric
