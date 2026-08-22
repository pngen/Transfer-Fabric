#include "transfer_fabric/integrity.hpp"

#include <cstring>

namespace transfer_fabric {

// ---- CRC32C (Castagnoli) ---------------------------------------------
const std::array<std::uint32_t, 256>& Crc32c::table() noexcept {
    static const std::array<std::uint32_t, 256> t = [] {
        std::array<std::uint32_t, 256> tbl{};
        const std::uint32_t poly = 0x82F63B78u; // reflected 0x1EDC6F41
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (poly ^ (c >> 1)) : (c >> 1);
            tbl[i] = c;
        }
        return tbl;
    }();
    return t;
}

void Crc32c::update(const std::uint8_t* data, byte_count len) noexcept {
    const auto& t = table();
    std::uint32_t c = crc_;
    for (byte_count i = 0; i < len; ++i)
        c = t[(c ^ data[i]) & 0xFFu] ^ (c >> 8);
    crc_ = c;
}

std::uint32_t Crc32c::compute(const void* data, byte_count len) noexcept {
    Crc32c h;
    h.update(data, len);
    return h.value();
}

// ---- SHA256 (FIPS 180-4) --------------------------------------------
namespace {
constexpr std::array<std::uint32_t, 64> K = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

inline std::uint32_t rotr(std::uint32_t x, unsigned n) noexcept {
    return (x >> n) | (x << (32u - n));
}
inline std::uint32_t ch(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (~x & z);
}
inline std::uint32_t maj(std::uint32_t x, std::uint32_t y, std::uint32_t z) noexcept {
    return (x & y) ^ (x & z) ^ (y & z);
}
} // namespace

void Sha256::reset() noexcept {
    state_ = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u};
    buffer_len_ = 0;
    total_len_ = 0;
}

void Sha256::process_block(const std::uint8_t* p) noexcept {
    std::array<std::uint32_t, 64> w{};
    for (int i = 0; i < 16; ++i) {
        w[static_cast<std::size_t>(i)] = (std::uint32_t(p[i*4]) << 24) | (std::uint32_t(p[i*4+1]) << 16)
                                       | (std::uint32_t(p[i*4+2]) << 8) | std::uint32_t(p[i*4+3]);
    }
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = rotr(w[static_cast<std::size_t>(i-15)], 7) ^ rotr(w[static_cast<std::size_t>(i-15)], 18) ^ (w[static_cast<std::size_t>(i-15)] >> 3);
        std::uint32_t s1 = rotr(w[static_cast<std::size_t>(i-2)], 17) ^ rotr(w[static_cast<std::size_t>(i-2)], 19) ^ (w[static_cast<std::size_t>(i-2)] >> 10);
        w[static_cast<std::size_t>(i)] = w[static_cast<std::size_t>(i-16)] + s0 + w[static_cast<std::size_t>(i-7)] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        std::uint32_t temp1 = h + S1 + ch(e,f,g) + K[static_cast<std::size_t>(i)] + w[static_cast<std::size_t>(i)];
        std::uint32_t S0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        std::uint32_t temp2 = S0 + maj(a,b,c);
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state_[0] += a; state_[1] += b; state_[2] += c; state_[3] += d;
    state_[4] += e; state_[5] += f; state_[6] += g; state_[7] += h;
}

void Sha256::update(const std::uint8_t* data, byte_count len) noexcept {
    total_len_ += len;
    std::size_t i = 0;
    if (buffer_len_ > 0) {
        while (i < len && buffer_len_ < 64) buffer_[buffer_len_++] = data[i++];
        if (buffer_len_ == 64) { process_block(buffer_.data()); buffer_len_ = 0; }
    }
    while (i + 64 <= len) { process_block(data + i); i += 64; }
    while (i < len) buffer_[buffer_len_++] = data[i++];
}

void Sha256::final(std::uint8_t* out32) noexcept {
    std::uint64_t bitlen = total_len_ * 8u;
    std::uint8_t pad = 0x80;
    update(&pad, 1);
    std::uint8_t zero = 0;
    while (buffer_len_ != 56) update(&zero, 1);
    std::uint8_t lenb[8];
    for (int k = 0; k < 8; ++k) lenb[k] = std::uint8_t((bitlen >> (56 - 8*k)) & 0xFF);
    update(lenb, 8);
    for (int k = 0; k < 8; ++k) {
        out32[k*4]   = std::uint8_t((state_[static_cast<std::size_t>(k)] >> 24) & 0xFF);
        out32[k*4+1] = std::uint8_t((state_[static_cast<std::size_t>(k)] >> 16) & 0xFF);
        out32[k*4+2] = std::uint8_t((state_[static_cast<std::size_t>(k)] >> 8) & 0xFF);
        out32[k*4+3] = std::uint8_t(state_[static_cast<std::size_t>(k)] & 0xFF);
    }
}

std::array<std::uint8_t, 32> Sha256::digest() noexcept {
    std::array<std::uint8_t, 32> out{};
    final(out.data());
    return out;
}

std::array<std::uint8_t, 32> Sha256::compute(const void* data, byte_count len) noexcept {
    Sha256 h;
    h.update(data, len);
    return h.digest();
}

std::string Sha256::hex(const std::array<std::uint8_t, 32>& d) noexcept {
    static const char* digits = "0123456789abcdef";
    std::string s(64, '0');
    for (int i = 0; i < 32; ++i) {
        s[static_cast<std::size_t>(i*2)]   = digits[(d[static_cast<std::size_t>(i)] >> 4) & 0xF];
        s[static_cast<std::size_t>(i*2+1)] = digits[d[static_cast<std::size_t>(i)] & 0xF];
    }
    return s;
}

std::string hex_encode(const std::uint8_t* data, std::size_t len) noexcept {
    static const char* digits = "0123456789abcdef";
    std::string s(len * 2, '0');
    for (std::size_t i = 0; i < len; ++i) {
        s[i*2]   = digits[(data[i] >> 4) & 0xF];
        s[i*2+1] = digits[data[i] & 0xF];
    }
    return s;
}

} // namespace transfer_fabric
