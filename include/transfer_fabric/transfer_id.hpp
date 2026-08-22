#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <functional>

#include "transfer_fabric/export.hpp"

namespace transfer_fabric {

// Stable 128-bit transfer identity. Value type; copies are cheap.
// A null id (all zero) is considered invalid/unset and is rejected by the runtime.
class TF_API TransferId {
public:
    constexpr TransferId() noexcept = default;
    constexpr TransferId(std::uint64_t hi, std::uint64_t lo) noexcept : hi_(hi), lo_(lo) {}

    constexpr bool is_null() const noexcept { return hi_ == 0 && lo_ == 0; }
    constexpr bool valid() const noexcept { return !is_null(); }

    constexpr std::uint64_t hi() const noexcept { return hi_; }
    constexpr std::uint64_t lo() const noexcept { return lo_; }

    // Deterministic derivation of a child id from a parent id plus a 64-bit salt.
    constexpr TransferId derive(std::uint64_t salt) const noexcept {
        return TransferId(hi_ ^ (lo_ + salt) ^ (salt * 0x9E3779B97F4A7C15ULL),
                          lo_ ^ (salt ^ (hi_ * 0x9E3779B97F4A7C15ULL)));
    }

    std::string to_string() const;

    // Parse a 32-hex-digit string. Rejects anything else. Returns false on bad input.
    static bool parse(const std::string& s, TransferId& out);

    // Generate a fresh, high-entropy id. Uses a process counter + OS randomness.
    static TransferId generate();

    friend constexpr bool operator==(const TransferId& a, const TransferId& b) noexcept {
        return a.hi_ == b.hi_ && a.lo_ == b.lo_;
    }
    friend constexpr bool operator!=(const TransferId& a, const TransferId& b) noexcept {
        return !(a == b);
    }
    friend constexpr bool operator<(const TransferId& a, const TransferId& b) noexcept {
        return a.hi_ < b.hi_ || (a.hi_ == b.hi_ && a.lo_ < b.lo_);
    }

private:
    std::uint64_t hi_{0};
    std::uint64_t lo_{0};
};

struct TransferIdHash {
    std::size_t operator()(const TransferId& id) const noexcept {
        // 64-bit mixing of both halves.
        std::uint64_t x = id.lo();
        x ^= id.hi() + 0x9E3779B97F4A7C15ULL + (x << 6) + (x >> 2);
        return static_cast<std::size_t>(x);
    }
};

} // namespace transfer_fabric

namespace std {
template<> struct hash<transfer_fabric::TransferId> {
    std::size_t operator()(const transfer_fabric::TransferId& id) const noexcept {
        std::uint64_t x = id.lo();
        x ^= id.hi() + 0x9E3779B97F4A7C15ULL + (x << 6) + (x >> 2);
        return static_cast<std::size_t>(x);
    }
};
} // namespace std
