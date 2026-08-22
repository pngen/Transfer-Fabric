#include "transfer_fabric/transports/frame.hpp"

#include <cstring>

#include "transfer_fabric/integrity.hpp"

namespace transfer_fabric {

namespace {
void put16(std::uint8_t* p, std::uint16_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
}
void put32(std::uint8_t* p, std::uint32_t v) noexcept {
    p[0] = static_cast<std::uint8_t>(v & 0xFF);
    p[1] = static_cast<std::uint8_t>((v >> 8) & 0xFF);
    p[2] = static_cast<std::uint8_t>((v >> 16) & 0xFF);
    p[3] = static_cast<std::uint8_t>((v >> 24) & 0xFF);
}
void put64(std::uint8_t* p, std::uint64_t v) noexcept {
    for (int i = 0; i < 8; ++i) p[i] = static_cast<std::uint8_t>((v >> (8*i)) & 0xFF);
}
std::uint16_t get16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}
std::uint32_t get32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
         | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}
std::uint64_t get64(const std::uint8_t* p) noexcept {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= static_cast<std::uint64_t>(p[i]) << (8*i);
    return v;
}
} // namespace

Error FrameEncoder::encode(const FrameHeader& h, const void* payload,
                           std::size_t payload_len, std::vector<std::uint8_t>& out) {
    if (payload_len > Protocol::kMaxPayload) {
        return Error(ErrorCategory::invalid_request, "frame payload exceeds max");
    }
    out.clear();
    out.resize(frame_layout::kHeaderSize + payload_len, 0);
    std::uint8_t* p = out.data();
    std::memset(p, 0, frame_layout::kHeaderSize);
    put32(p + frame_layout::kMagicOff, Protocol::kMagic);
    put16(p + frame_layout::kVersionOff, Protocol::kVersion);
    p[frame_layout::kTypeOff] = static_cast<std::uint8_t>(h.type);
    p[frame_layout::kFlagsOff] = h.flags;
    put32(p + frame_layout::kPayloadLenOff, static_cast<std::uint32_t>(payload_len));
    put64(p + frame_layout::kTransferIdOff, h.transfer_id.hi());
    put64(p + frame_layout::kTransferIdOff + 8, h.transfer_id.lo());
    put64(p + frame_layout::kChunkIdOff, h.chunk_id);
    put64(p + frame_layout::kOffsetOff, h.offset);
    put64(p + frame_layout::kLengthOff, h.length);
    if (payload && payload_len) {
        std::memcpy(p + frame_layout::kHeaderSize, payload, payload_len);
        std::uint32_t crc = Crc32c::compute(payload, payload_len);
        put32(p + frame_layout::kCrcOff, crc);
    } else {
        put32(p + frame_layout::kCrcOff, 0);
    }
    return Error();
}

std::optional<DecodedFrame> FrameDecoder::feed(const std::uint8_t* data, std::size_t len, Error& err) {
    if (data && len) buf_.insert(buf_.end(), data, data + len);
    if (buf_.size() < frame_layout::kHeaderSize) return std::nullopt;
    const std::uint8_t* p = buf_.data();
    std::uint32_t magic = get32(p + frame_layout::kMagicOff);
    if (magic != Protocol::kMagic) {
        err = Error(ErrorCategory::invalid_request, "bad frame magic");
        return std::nullopt;
    }
    std::uint16_t ver = get16(p + frame_layout::kVersionOff);
    if (ver != Protocol::kVersion) {
        err = Error(ErrorCategory::invalid_request, "bad frame version");
        return std::nullopt;
    }
    std::uint32_t payload_len = get32(p + frame_layout::kPayloadLenOff);
    if (payload_len > Protocol::kMaxPayload) {
        err = Error(ErrorCategory::invalid_request, "frame payload too large");
        return std::nullopt;
    }
    if (buf_.size() < frame_layout::kHeaderSize + payload_len) return std::nullopt;

    DecodedFrame f;
    f.header.magic = magic;
    f.header.version = ver;
    f.header.type = p[frame_layout::kTypeOff];
    f.header.flags = p[frame_layout::kFlagsOff];
    f.header.payload_length = payload_len;
    f.header.transfer_id = TransferId(get64(p + frame_layout::kTransferIdOff),
                                      get64(p + frame_layout::kTransferIdOff + 8));
    f.header.chunk_id = get64(p + frame_layout::kChunkIdOff);
    f.header.offset = get64(p + frame_layout::kOffsetOff);
    f.header.length = get64(p + frame_layout::kLengthOff);
    f.header.payload_crc32c = get32(p + frame_layout::kCrcOff);

    if (payload_len) {
        const std::uint8_t* pl = p + frame_layout::kHeaderSize;
        std::uint32_t crc = Crc32c::compute(pl, payload_len);
        if (f.header.payload_crc32c != 0 && crc != f.header.payload_crc32c) {
            err = Error(ErrorCategory::integrity_failure, "frame payload CRC mismatch");
            return std::nullopt;
        }
        f.payload.assign(pl, pl + payload_len);
    }

    buf_.erase(buf_.begin(), buf_.begin() + (frame_layout::kHeaderSize + payload_len));
    return f;
}

} // namespace transfer_fabric
