#include "transfer_fabric/transfer_id.hpp"

#include <array>
#include <cstdio>
#include <random>

namespace transfer_fabric {

namespace {
// Process-level entropy source. First block uses OS randomness; afterwards a
// monotonic counter + entropy mixing keeps ids collision-resistant without
// blocking and without relying on global random_device state.
std::uint64_t next_entropy() {
    static std::uint64_t counter = []() {
        std::random_device rd;
        std::uint64_t seed = (std::uint64_t(rd()) << 32) ^ rd();
        return seed;
    }();
    counter += 0x9E3779B97F4A7C15ULL;
    std::uint64_t z = counter;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}
} // namespace

std::string TransferId::to_string() const {
    std::array<char, 33> buf{};
    std::snprintf(buf.data(), buf.size(), "%016llx%016llx",
                  static_cast<unsigned long long>(hi_),
                  static_cast<unsigned long long>(lo_));
    return std::string(buf.data(), 32);
}

bool TransferId::parse(const std::string& s, TransferId& out) {
    if (s.size() != 32) return false;
    std::uint64_t hi = 0, lo = 0;
    auto hex = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
        return 255;
    };
    for (int i = 0; i < 32; ++i) {
        unsigned v = hex(s[static_cast<std::size_t>(i)]);
        if (v == 255) return false;
        if (i < 16) hi = (hi << 4) | v;
        else lo = (lo << 4) | v;
    }
    out = TransferId(hi, lo);
    return true;
}

TransferId TransferId::generate() {
    std::uint64_t hi = next_entropy();
    std::uint64_t lo = next_entropy();
    return TransferId(hi, lo);
}

} // namespace transfer_fabric
