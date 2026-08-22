#include "transfer_fabric/transports/tcp.hpp"

#include <cstring>

#include "transfer_fabric/integrity.hpp"
#include "transfer_fabric/chunking.hpp"

#if defined(_WIN32)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#endif

namespace transfer_fabric {

#if defined(_WIN32)
namespace {
struct WsaGuard {
    WsaGuard() {
        WSADATA d;
        WSAStartup(MAKEWORD(2, 2), &d);
    }
    ~WsaGuard() { WSACleanup(); }
};
void ensure_wsa() { static WsaGuard g; (void)g; }
using sock_t = SOCKET;
constexpr sock_t kInvalid = INVALID_SOCKET;
} // namespace

namespace {
Error last_socket_error(const char* what) {
    return Error(ErrorCategory::transient_transport, WSAGetLastError(), what);
}
bool send_all(sock_t s, const void* buf, std::size_t n) {
    const char* p = static_cast<const char*>(buf);
    std::size_t sent = 0;
    while (sent < n) {
        int r = ::send(s, p + sent, static_cast<int>(n - sent), 0);
        if (r == SOCKET_ERROR) return false;
        if (r == 0) return false;
        sent += static_cast<std::size_t>(r);
    }
    return true;
}
bool recv_all(sock_t s, void* buf, std::size_t n) {
    char* p = static_cast<char*>(buf);
    std::size_t got = 0;
    while (got < n) {
        int r = ::recv(s, p + got, static_cast<int>(n - got), 0);
        if (r == 0) return false;          // disconnect
        if (r == SOCKET_ERROR) return false;
        got += static_cast<std::size_t>(r);
    }
    return true;
}
bool recv_some(sock_t s, std::string& out) {
    char tmp[4096];
    int r = ::recv(s, tmp, sizeof(tmp), 0);
    if (r <= 0) return false;
    out.append(tmp, static_cast<std::size_t>(r));
    return true;
}
} // namespace

TcpTransport::TcpTransport() { ensure_wsa(); }
TcpTransport::~TcpTransport() { }

// ---- helpers to build/parse a simple binary transfer_req payload ---------
std::vector<std::uint8_t> encode_req_payload(const std::string& path) {
    std::vector<std::uint8_t> out;
    std::uint32_t n = static_cast<std::uint32_t>(path.size());
    out.push_back(static_cast<std::uint8_t>(n & 0xFF));
    out.push_back(static_cast<std::uint8_t>((n >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((n >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((n >> 24) & 0xFF));
    out.insert(out.end(), path.begin(), path.end());
    return out;
}
Error decode_req_payload(const std::vector<std::uint8_t>& pl, std::string& path) {
    if (pl.size() < 4) return Error(ErrorCategory::invalid_request, "transfer_req payload truncated");
    std::uint32_t n = static_cast<std::uint32_t>(pl[0]) | (static_cast<std::uint32_t>(pl[1]) << 8)
                    | (static_cast<std::uint32_t>(pl[2]) << 16) | (static_cast<std::uint32_t>(pl[3]) << 24);
    if (static_cast<std::size_t>(n) + 4 > pl.size()) return Error(ErrorCategory::invalid_request, "transfer_req path length invalid");
    path.assign(reinterpret_cast<const char*>(pl.data() + 4), n);
    return Error();
}

namespace {
bool open_read_file(const std::string& path, std::uint64_t& handle, Error& err) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = Error(ErrorCategory::backend_failure, "cannot open source file"); return false; }
    handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(h));
    return true;
}
bool open_write_file(const std::string& path, std::uint64_t& handle, Error& err) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { err = Error(ErrorCategory::backend_failure, "cannot open destination file"); return false; }
    handle = static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(h));
    return true;
}
} // namespace
#endif

Error TcpChunkServer::listen(std::uint16_t port) {
#if defined(_WIN32)
    ensure_wsa();
    sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) return last_socket_error("socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(port);
    if (::bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(s); return last_socket_error("bind");
    }
    if (::listen(s, 1) == SOCKET_ERROR) {
        ::closesocket(s); return last_socket_error("listen");
    }
    listen_sock_ = static_cast<std::uintptr_t>(s);
    port_ = port;
    return Error();
#else
    (void)port; return Error(ErrorCategory::unsupported_path, "tcp server not implemented on POSIX");
#endif
}

void TcpChunkServer::close() {
#if defined(_WIN32)
    if (listen_sock_) { ::closesocket(static_cast<sock_t>(listen_sock_)); listen_sock_ = 0; }
#endif
}

Error TcpChunkServer::serve_once() {
#if defined(_WIN32)
    sock_t ls = static_cast<sock_t>(listen_sock_);
    if (ls == kInvalid || ls == 0) return Error(ErrorCategory::invalid_request, "server not listening");
    sockaddr_in cli{};
    int clilen = static_cast<int>(sizeof(cli));
    sock_t cs = ::accept(ls, reinterpret_cast<sockaddr*>(&cli), &clilen);
    if (cs == kInvalid) return last_socket_error("accept");

    FrameDecoder dec;
    // handshake
    {
        while (true) {
            std::string chunkbuf;
            if (!recv_some(cs, chunkbuf)) { ::closesocket(cs); return last_socket_error("recv"); }
            Error e;
            auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
            if (!f) {
                if (!e.ok()) { ::closesocket(cs); return e; }
                continue;
            }
            if (f->header.type == static_cast<std::uint8_t>(FrameType::hello)) {
                FrameHeader ack; ack.type = static_cast<std::uint8_t>(FrameType::hello_ack);
                std::vector<std::uint8_t> out;
                FrameEncoder::encode(ack, nullptr, 0, out);
                send_all(cs, out.data(), out.size());
                break;
            }
        }
    }
    // read transfer_req
    std::string src_path; byte_offset req_offset=0; byte_count req_len=0; TransferId tid;
    {
        while (true) {
            std::string chunkbuf;
            if (!recv_some(cs, chunkbuf)) { ::closesocket(cs); return last_socket_error("recv"); }
            Error e;
            auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
            if (!f) { if (!e.ok()) { ::closesocket(cs); return e; } continue; }
            if (f->header.type == static_cast<std::uint8_t>(FrameType::transfer_req)) {
                Error pe = decode_req_payload(f->payload, src_path);
                if (!pe.ok()) { ::closesocket(cs); return pe; }
                req_offset = f->header.offset;
                req_len = f->header.length;
                tid = f->header.transfer_id;
                break;
            }
        }
    }
    std::uint64_t fh = 0; Error fe;
    if (!open_read_file(src_path, fh, fe)) { ::closesocket(cs); return fe; }
    bool ok = true;
    ChunkPlanInput ci;
    ci.total = req_len;
    ci.source_offset = 0;
    ci.dest_offset = req_offset;
    ci.min_chunk = 4096;
    ci.max_chunk = Protocol::kMaxPayload;
    ci.preferred_chunk = Protocol::kMaxPayload;
    ci.alignment = 4096;
    ChunkPlan plan;
    try { plan = ChunkPlan::build(ci); }
    catch (...) { CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(fh))); ::closesocket(cs); return Error(ErrorCategory::invalid_request, "invalid chunk plan"); }
    for (byte_count i = 0; i < plan.chunk_count(); ++i) {
        Chunk c; plan.chunk_at(i, c);
        std::vector<std::uint8_t> payload(static_cast<std::size_t>(c.length));
        HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(fh));
        LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(c.source_offset);
        SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
        DWORD rd = 0;
        if (!ReadFile(h, payload.data(), static_cast<DWORD>(c.length), &rd, nullptr) ||
            static_cast<byte_count>(rd) != c.length) {
            ok = false; break;
        }
        FrameHeader fhdr;
        fhdr.type = static_cast<std::uint8_t>(FrameType::chunk);
        fhdr.transfer_id = tid;
        fhdr.chunk_id = c.index;
        fhdr.offset = c.source_offset;
        fhdr.length = c.length;
        std::vector<std::uint8_t> out;
        Error ee = FrameEncoder::encode(fhdr, payload.data(), payload.size(), out);
        if (!ee.ok()) { ok = false; break; }
        if (!send_all(cs, out.data(), out.size())) { ok = false; break; }
        // wait for ack
        while (true) {
            std::string chunkbuf;
            if (!recv_some(cs, chunkbuf)) { ok = false; break; }
            Error e;
            auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
            if (!f) { if (!e.ok()) { ok = false; } continue; }
            if (f->header.type == static_cast<std::uint8_t>(FrameType::chunk_ack)) break;
            if (f->header.type == static_cast<std::uint8_t>(FrameType::error)) { ok = false; break; }
        }
        if (!ok) break;
    }
    CloseHandle(reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(fh)));
    if (ok) {
        FrameHeader commit; commit.type = static_cast<std::uint8_t>(FrameType::commit); commit.transfer_id = tid;
        std::vector<std::uint8_t> out;
        FrameEncoder::encode(commit, nullptr, 0, out);
        send_all(cs, out.data(), out.size());
        // wait commit_ack
        while (true) {
            std::string chunkbuf;
            if (!recv_some(cs, chunkbuf)) break;
            Error e;
            auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
            if (!f) { if (!e.ok()) break; continue; }
            if (f->header.type == static_cast<std::uint8_t>(FrameType::commit_ack)) break;
        }
    }
    ::closesocket(cs);
    return ok ? Error() : Error(ErrorCategory::backend_failure, "transfer aborted");
#else
    return Error(ErrorCategory::unsupported_path, "tcp server not implemented on POSIX");
#endif
}

Error TcpChunkClient::connect(const std::string& host, std::uint16_t port) {
#if defined(_WIN32)
    ensure_wsa();
    sock_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == kInvalid) return last_socket_error("socket");
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) { ::closesocket(s); return Error(ErrorCategory::invalid_request, "bad host"); }
    if (::connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        ::closesocket(s); return last_socket_error("connect");
    }
    sock_ = static_cast<std::uintptr_t>(s);
    host_ = host; port_ = port;
    return Error();
#else
    (void)host; (void)port; return Error(ErrorCategory::unsupported_path, "tcp client not implemented");
#endif
}

void TcpChunkClient::close() {
#if defined(_WIN32)
    if (sock_) { ::closesocket(static_cast<sock_t>(sock_)); sock_ = 0; }
#endif
}

Error TcpChunkClient::transfer(const std::string& src_path, byte_offset offset,
                               byte_count length, const std::string& dst_path,
                               const TransferId& id) {
#if defined(_WIN32)
    sock_t cs = static_cast<sock_t>(sock_);
    if (cs == kInvalid || cs == 0) return Error(ErrorCategory::invalid_request, "not connected");
    FrameDecoder dec;
    // hello
    {
        FrameHeader hello; hello.type = static_cast<std::uint8_t>(FrameType::hello);
        std::vector<std::uint8_t> out;
        FrameEncoder::encode(hello, nullptr, 0, out);
        send_all(cs, out.data(), out.size());
        while (true) {
            std::string chunkbuf;
            if (!recv_some(cs, chunkbuf)) return last_socket_error("recv");
            Error e;
            auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
            if (!f) { if (!e.ok()) return e; continue; }
            if (f->header.type == static_cast<std::uint8_t>(FrameType::hello_ack)) break;
        }
    }
    // transfer_req
    {
        FrameHeader req; req.type = static_cast<std::uint8_t>(FrameType::transfer_req);
        req.transfer_id = id; req.offset = offset; req.length = length;
        auto payload = encode_req_payload(src_path);
        std::vector<std::uint8_t> out;
        FrameEncoder::encode(req, payload.data(), payload.size(), out);
        if (!send_all(cs, out.data(), out.size())) return last_socket_error("send");
    }
    std::uint64_t dh = 0; Error we;
    if (!open_write_file(dst_path, dh, we)) return we;
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(dh));
    byte_count written = 0;
    bool done = false; Error result;
    std::uint64_t expected_chunk = 0;
    while (!done) {
        std::string chunkbuf;
        if (!recv_some(cs, chunkbuf)) { result = last_socket_error("recv"); break; }
        Error e;
        auto f = dec.feed(reinterpret_cast<const std::uint8_t*>(chunkbuf.data()), chunkbuf.size(), e);
        if (!f) { if (!e.ok()) { result = e; break; } continue; }
        FrameType t = static_cast<FrameType>(f->header.type);
        if (t == FrameType::chunk) {
            // Validate ordering, id, and bounds.
            if (f->header.chunk_id != expected_chunk) { result = Error(ErrorCategory::invalid_request, "out-of-order or duplicate chunk"); break; }
            if (f->header.offset < offset || f->header.offset + f->header.length > offset + length) { result = Error(ErrorCategory::invalid_request, "chunk outside requested range"); break; }
            LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(f->header.offset);
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            DWORD wr = 0;
            if (!WriteFile(h, f->payload.data(), static_cast<DWORD>(f->payload.size()), &wr, nullptr) || wr != f->payload.size()) {
                result = Error(ErrorCategory::backend_failure, "write failed"); break;
            }
            written += f->payload.size();
            ++expected_chunk;
            FrameHeader ack; ack.type = static_cast<std::uint8_t>(FrameType::chunk_ack);
            ack.transfer_id = id; ack.chunk_id = f->header.chunk_id;
            std::vector<std::uint8_t> out;
            FrameEncoder::encode(ack, nullptr, 0, out);
            send_all(cs, out.data(), out.size());
        } else if (t == FrameType::commit) {
            done = true;
        } else if (t == FrameType::error) {
            result = Error(ErrorCategory::backend_failure, std::string("peer error: ") + (f->payload.empty() ? "" : std::string(f->payload.begin(), f->payload.end())));
            break;
        } else {
            // ignore other frames
        }
    }
    CloseHandle(h);
    if (done) {
        FrameHeader ack; ack.type = static_cast<std::uint8_t>(FrameType::commit_ack); ack.transfer_id = id;
        std::vector<std::uint8_t> out;
        FrameEncoder::encode(ack, nullptr, 0, out);
        send_all(cs, out.data(), out.size());
        if (written != length) result = Error(ErrorCategory::integrity_failure, "received byte count mismatch");
    }
    if (result.ok() && !done) result = Error(ErrorCategory::transient_transport, "connection closed before commit");
    return result;
#else
    (void)src_path;(void)offset;(void)length;(void)dst_path;(void)id;
    return Error(ErrorCategory::unsupported_path, "tcp client not implemented");
#endif
}

} // namespace transfer_fabric
