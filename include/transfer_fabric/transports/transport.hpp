#pragma once

#include <cstdint>
#include <string>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/transfer_id.hpp"

namespace transfer_fabric {

// Framed message types for the minimal distributed transfer transport.
enum class TF_API FrameType : std::uint8_t {
    hello        = 1,
    hello_ack    = 2,
    transfer_req = 3,
    chunk        = 4,
    chunk_ack    = 5,
    commit       = 6,
    commit_ack   = 7,
    error        = 8,
    bye          = 9,
};

TF_API inline const char* to_string(FrameType t) noexcept {
    switch (t) {
        case FrameType::hello:        return "hello";
        case FrameType::hello_ack:    return "hello_ack";
        case FrameType::transfer_req: return "transfer_req";
        case FrameType::chunk:        return "chunk";
        case FrameType::chunk_ack:    return "chunk_ack";
        case FrameType::commit:       return "commit";
        case FrameType::commit_ack:   return "commit_ack";
        case FrameType::error:        return "error";
        case FrameType::bye:          return "bye";
    }
    return "unknown";
}

// Fixed-size header prefix of every framed message. Byte layout is explicit and
// endian-converted on the wire so peers are not coupled to host byte order.
// All lengths are validated against the protocol maximum before any allocation.
struct TF_API FrameHeader {
    std::uint32_t magic{0x54465200u};      // 'T','F','R' + 0
    std::uint16_t version{1};
    std::uint8_t  type{0};
    std::uint8_t  flags{0};
    std::uint32_t payload_length{0};       // bounded by kMaxPayload
    TransferId    transfer_id{};
    std::uint64_t chunk_id{0};
    std::uint64_t offset{0};
    std::uint64_t length{0};
    std::uint32_t payload_crc32c{0};       // over payload bytes
};

// Protocol constants.
struct TF_API Protocol {
    static constexpr std::uint32_t kMagic = 0x54465200u;
    static constexpr std::uint16_t kVersion = 1;
    static constexpr std::uint32_t kHeaderSize = 64;
    static constexpr std::uint32_t kMaxPayload = 16u * 1024u * 1024u; // 16 MiB frame
    static constexpr std::uint32_t kMaxFrames = 8u * 1024u * 1024u;
    static constexpr std::uint16_t kDefaultPort = 45634;
};

// An abstract framed transport connection. Concrete transports implement the
// byte plumbing; the framing/validation logic is transport-neutral.
class TF_API Transport {
public:
    virtual ~Transport() = default;
    virtual std::string name() const = 0;
};

} // namespace transfer_fabric
