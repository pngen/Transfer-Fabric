#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "transfer_fabric/export.hpp"

namespace transfer_fabric {

using byte_count = std::uint64_t;
using byte_offset = std::uint64_t;
using generation_t = std::uint64_t;
using device_id_t = std::uint32_t;

// Memory/domain classification of an endpoint. Never the canonical allocator.
enum class TF_API MemoryDomain : std::uint8_t {
    unknown        = 0,
    host_pageable,   // plain, paged host memory
    host_pinned,     // pinned (locked) host memory
    device,          // accelerator device memory
    shared,          // process-shared memory region
    storage,         // file/object storage region
    remote,          // logical remote endpoint
    mmap,            // memory-mapped file region
};

TF_API inline const char* to_string(MemoryDomain d) noexcept {
    switch (d) {
        case MemoryDomain::host_pageable: return "host_pageable";
        case MemoryDomain::host_pinned:   return "host_pinned";
        case MemoryDomain::device:        return "device";
        case MemoryDomain::shared:        return "shared";
        case MemoryDomain::storage:       return "storage";
        case MemoryDomain::remote:        return "remote";
        case MemoryDomain::mmap:          return "mmap";
        case MemoryDomain::unknown:       return "unknown";
    }
    return "unknown";
}

// Staging pool kinds the transfer engine may use. These are transfer-owned and
// must not be confused with a general allocator.
enum class TF_API PoolKind : std::uint8_t {
    none        = 0,
    host,         // pageable host staging
    host_pinned,  // pinned host staging
    device,       // temporary device staging
};

TF_API inline const char* to_string(PoolKind p) noexcept {
    switch (p) {
        case PoolKind::none:        return "none";
        case PoolKind::host:        return "host";
        case PoolKind::host_pinned: return "host_pinned";
        case PoolKind::device:      return "device";
    }
    return "none";
}

// A bounded, checked half-open byte range [offset, offset+length).
struct TF_API ByteRange {
    byte_offset offset{0};
    byte_count  length{0};

    constexpr bool empty() const noexcept { return length == 0; }
    constexpr byte_offset end() const noexcept { return offset + length; }

    // Overflow-checked length computation. Returns false on overflow.
    constexpr bool end_checked(byte_offset& out) const noexcept {
        if (length > (byte_count(~byte_offset{0}) - offset)) return false;
        out = offset + length;
        return true;
    }

    // Return the sub-range [start, start+count) within this range, validated.
    // Returns false if the request is out of bounds or overflows.
    bool subrange(byte_offset start, byte_count count, ByteRange& out) const noexcept {
        byte_offset end;
        if (!end_checked(end)) return false;
        if (start > end) return false;
        if (count > (end - start)) return false;
        out.offset = offset + start;
        out.length = count;
        return true;
    }

    friend constexpr bool operator==(const ByteRange&, const ByteRange&) = default;
};

// Overflow-safe checked addition for byte counts.
constexpr bool checked_add(byte_count a, byte_count b, byte_count& out) noexcept {
    if (b > (byte_count(~byte_offset{0}) - a)) return false;
    out = a + b;
    return true;
}

constexpr bool checked_mul(byte_count a, byte_count b, byte_count& out) noexcept {
    if (a != 0 && b > (byte_count(~byte_offset{0}) / a)) return false;
    out = a * b;
    return true;
}

// Align a length up to an alignment (power-of-two expected but handled
// generally with overflow safety). Returns false if alignment would overflow.
constexpr bool align_up(byte_count value, byte_count alignment, byte_count& out) noexcept {
    if (alignment == 0) return false;
    byte_count rem = value % alignment;
    if (rem == 0) { out = value; return true; }
    byte_count padded = value + (alignment - rem);
    if (padded < value) return false; // overflow
    out = padded;
    return true;
}

} // namespace transfer_fabric
