#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/transports/transport.hpp"
#include "transfer_fabric/transports/frame.hpp"
#include "transfer_fabric/transfer_id.hpp"

namespace transfer_fabric {

// Framed TCP transport. It is a real, minimal distributed-transfer transport:
// bounded frames, explicit message types, CRC-protected payloads, acks, error
// frames, malformed-frame rejection, and partial-frame handling. It is not a
// replacement for RDMA or a future high-performance transport.
class TF_API TcpTransport : public Transport {
public:
    TcpTransport();
    ~TcpTransport();
    std::string name() const override { return "tcp"; }

    // A host:port endpoint.
    struct Address { std::string host; std::uint16_t port; };

    // Returns the default loopback port for tests/examples.
    static std::uint16_t default_port() noexcept { return Protocol::kDefaultPort; }
};

// Server side: serves a file region (source) to a single client. It streams the
// region in bounded chunk frames, waits for acks, and commits. Handles protocol
// errors and disconnect cleanly.
class TF_API TcpChunkServer {
public:
    // Binds and listens on the given port. Returns 0 on success.
    Error listen(std::uint16_t port);
    // Accepts and serves exactly one transfer request, then closes. On success
    // the region has been sent. The server reads the file lazily per chunk.
    Error serve_once();
    // Stop listening (closes the listen socket).
    void close();
private:
    std::uintptr_t listen_sock_{0};
    std::uint16_t port_{0};
};

// Client side: connects to a server and pulls a file region into a local file.
class TF_API TcpChunkClient {
public:
    Error connect(const std::string& host, std::uint16_t port);
    // Request transfer of [offset, offset+length) of server-side src_path into
    // local dst_path. Blocks until complete. Validates every frame and CRC.
    Error transfer(const std::string& src_path, byte_offset offset,
                   byte_count length, const std::string& dst_path,
                   const TransferId& id);
    void close();
private:
    std::uintptr_t sock_{0};
    std::uint16_t port_{0};
    std::string host_;
};

} // namespace transfer_fabric
