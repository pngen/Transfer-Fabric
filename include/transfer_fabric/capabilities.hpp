#pragma once

#include <cstdint>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Bitmask describing what an endpoint or a movement path can actually do.
// Unsupported combinations must be rejected explicitly by the runtime.
enum class TF_API Capability : std::uint32_t {
    none                 = 0,
    readable             = 1u << 0,
    writable             = 1u << 1,
    cpu_addressable      = 1u << 2,
    accelerator_addressable = 1u << 3,
    supports_dma         = 1u << 4,
    direct_copy          = 1u << 5,
    staged_copy          = 1u << 6,
    async_copy           = 1u << 7,
    peer_copy            = 1u << 8,
    overlap              = 1u << 9,
    multiprocess         = 1u << 10,
    persistent           = 1u << 11,
    verification         = 1u << 12,
};

TF_API constexpr std::uint32_t operator|(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b);
}
TF_API constexpr std::uint32_t operator&(Capability a, Capability b) noexcept {
    return static_cast<std::uint32_t>(a) & static_cast<std::uint32_t>(b);
}

// A narrow capability set over a fixed 64-bit flags word.
struct TF_API CapabilitySet {
    std::uint64_t flags{0};

    void set(Capability c) noexcept { flags |= static_cast<std::uint64_t>(c); }
    void unset(Capability c) noexcept { flags &= ~static_cast<std::uint64_t>(c); }
    bool has(Capability c) const noexcept { return (flags & static_cast<std::uint64_t>(c)) != 0; }
    bool has_any(std::uint64_t mask) const noexcept { return (flags & mask) != 0; }
    bool empty() const noexcept { return flags == 0; }
};

TF_API inline const char* to_string(Capability c) noexcept {
    switch (c) {
        case Capability::readable: return "readable";
        case Capability::writable: return "writable";
        case Capability::cpu_addressable: return "cpu_addressable";
        case Capability::accelerator_addressable: return "accelerator_addressable";
        case Capability::supports_dma: return "supports_dma";
        case Capability::direct_copy: return "direct_copy";
        case Capability::staged_copy: return "staged_copy";
        case Capability::async_copy: return "async_copy";
        case Capability::peer_copy: return "peer_copy";
        case Capability::overlap: return "overlap";
        case Capability::multiprocess: return "multiprocess";
        case Capability::persistent: return "persistent";
        case Capability::verification: return "verification";
        case Capability::none: return "none";
    }
    return "none";
}

// Numeric characteristics of an endpoint or path. These are real constraints the
// planner must respect, not decoration.
struct TF_API EndpointCapabilities {
    CapabilitySet capabilities;
    MemoryDomain   memory_domain{MemoryDomain::unknown};
    device_id_t    device_id{0};
    std::uint64_t  required_alignment{1};
    byte_count     max_transfer_size{byte_count(~0ULL)};
    byte_count     preferred_transfer_size{64u * 1024u};
    std::vector<byte_count> supported_chunk_sizes; // empty == all chunk sizes
    bool           dma_capable{false};
    bool           peer_capable{false};
    bool           is_local{false};
};

} // namespace transfer_fabric
