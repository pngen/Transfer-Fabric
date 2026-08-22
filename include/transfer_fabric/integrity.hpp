#pragma once

#include <cstdint>
#include <array>
#include <string>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Integrity verification modes. NONE disables verification; the runtime then
// records integrity as "not verified" rather than "verified clean".
enum class TF_API VerificationMode : std::uint8_t {
    none      = 0,
    crc32c,
    sha256,
};

TF_API inline const char* to_string(VerificationMode m) noexcept {
    switch (m) {
        case VerificationMode::none:   return "none";
        case VerificationMode::crc32c: return "crc32c";
        case VerificationMode::sha256: return "sha256";
    }
    return "none";
}

// Self-contained checksum/primitives. CRC32C is Castagnoli (poly 0x1EDC6F41,
// reflected). SHA256 is FIPS 180-4. Both accept incremental updates so per-chunk
// and whole-transfer verification can share code.
class TF_API Crc32c {
public:
    Crc32c() = default;
    void update(const std::uint8_t* data, byte_count len) noexcept;
    void update(const void* data, byte_count len) noexcept { update(static_cast<const std::uint8_t*>(data), len); }
    std::uint32_t value() const noexcept { return ~crc_; }
    void reset() noexcept { crc_ = 0xFFFFFFFFu; }
    static std::uint32_t compute(const void* data, byte_count len) noexcept;
private:
    std::uint32_t crc_{0xFFFFFFFFu};
    static const std::array<std::uint32_t, 256>& table() noexcept;
};

class TF_API Sha256 {
public:
    Sha256() { reset(); }
    void update(const std::uint8_t* data, byte_count len) noexcept;
    void update(const void* data, byte_count len) noexcept { update(static_cast<const std::uint8_t*>(data), len); }
    void final(std::uint8_t* out32) noexcept;
    std::array<std::uint8_t, 32> digest() noexcept;
    void reset() noexcept;
    static std::array<std::uint8_t, 32> compute(const void* data, byte_count len) noexcept;
    static std::string hex(const std::array<std::uint8_t, 32>& d) noexcept;
private:
    void process_block(const std::uint8_t* p) noexcept;
    std::array<std::uint32_t, 8> state_;
    std::array<std::uint8_t, 64> buffer_;
    std::uint64_t total_len_{0};
    std::size_t buffer_len_{0};
};

// Convenience: hex-encode an arbitrary byte array.
TF_API std::string hex_encode(const std::uint8_t* data, std::size_t len) noexcept;

} // namespace transfer_fabric
