#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/transports/transport.hpp"

namespace transfer_fabric {

// Wire layout constants (little-endian on the wire).
namespace frame_layout {
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kMagicOff = 0;         // u32
constexpr std::size_t kVersionOff = 4;       // u16
constexpr std::size_t kTypeOff = 6;          // u8
constexpr std::size_t kFlagsOff = 7;         // u8
constexpr std::size_t kPayloadLenOff = 8;    // u32
constexpr std::size_t kTransferIdOff = 12;   // 16 bytes (2x u64)
constexpr std::size_t kChunkIdOff = 28;      // u64
constexpr std::size_t kOffsetOff = 36;       // u64
constexpr std::size_t kLengthOff = 44;       // u64
constexpr std::size_t kCrcOff = 52;          // u32
constexpr std::size_t kReservedOff = 56;     // 8 bytes reserved
} // namespace frame_layout

// Encodes a FrameHeader (+ optional payload) into a contiguous wire buffer.
// Bounded: rejects payloads larger than Protocol::kMaxPayload.
class TF_API FrameEncoder {
public:
    static Error encode(const FrameHeader& header, const void* payload,
                        std::size_t payload_len, std::vector<std::uint8_t>& out);
};

// A fully decoded frame (header + payload).
struct TF_API DecodedFrame {
    FrameHeader header;
    std::vector<std::uint8_t> payload;
};

// Streaming frame decoder. Feed arbitrary byte chunks; it extracts complete,
// validated frames and reports malformed input as an error rather than
// allocating from untrusted lengths.
class TF_API FrameDecoder {
public:
    std::optional<DecodedFrame> feed(const std::uint8_t* data, std::size_t len, Error& err);
    void reset() { buf_.clear(); }

private:
    std::vector<std::uint8_t> buf_;
};

} // namespace transfer_fabric
