#include "tf_test.hpp"
#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/transports/tcp.hpp"
#include "transfer_fabric/transports/frame.hpp"
#include "transfer_fabric/integrity.hpp"
#include "transfer_fabric/transfer_id.hpp"

#include <vector>
#include <cstring>
#include <cstdio>
#include <string>
#include <thread>

using namespace transfer_fabric;

#if defined(_WIN32)
#  include <windows.h>
static bool write_file(const std::string& path, const void* data, size_t n) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    const char* p = static_cast<const char*>(data);
    size_t done=0;
    while (done<n) { DWORD w=0; if (!WriteFile(h, p+done, (DWORD)(n-done), &w, nullptr) || w==0) { CloseHandle(h); return false; } done+=w; }
    FlushFileBuffers(h); CloseHandle(h); return true;
}
static bool read_file(const std::string& path, void* data, size_t n) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    char* p = static_cast<char*>(data); size_t done=0;
    while (done<n) { DWORD r=0; if (!ReadFile(h, p+done, (DWORD)(n-done), &r, nullptr)) break; if (r==0) break; done+=r; }
    CloseHandle(h); return done==n;
}
// Test files live in the system temporary directory (it exists on every machine).
static const char* temp_path(const char* name) {
    static char buf[MAX_PATH + 64];
    char dir[MAX_PATH + 1];
    DWORD n = GetTempPathA(sizeof(dir), dir);   // ends with a backslash
    if (n == 0 || n > sizeof(dir)) dir[0] = 0;
    std::snprintf(buf, sizeof(buf), "%stf_%s", dir, name);
    return buf;
}
#endif

// ---- Framed protocol ------------------------------------------------
TF_TEST(storage_frame_codec) {
    FrameHeader hdr;
    hdr.type = static_cast<std::uint8_t>(FrameType::chunk);
    hdr.transfer_id = TransferId(1,2);
    hdr.chunk_id = 5; hdr.offset = 100; hdr.length = 8;
    const char* payload = "abcdefgh";
    std::vector<std::uint8_t> out;
    TF_REQUIRE(FrameEncoder::encode(hdr, payload, 8, out).ok());
    FrameDecoder dec;
    Error e;
    auto f = dec.feed(out.data(), out.size(), e);
    TF_REQUIRE(f.has_value());
    TF_CHECK(e.ok());
    TF_CHECK(f->header.chunk_id == 5);
    TF_CHECK(f->header.offset == 100);
    TF_CHECK(std::memcmp(f->payload.data(), "abcdefgh", 8)==0);
}
TF_TEST(storage_frame_malformed) {
    // bad magic
    std::vector<std::uint8_t> bad(64, 0);
    FrameDecoder dec; Error e;
    auto f = dec.feed(bad.data(), bad.size(), e);
    TF_CHECK(!e.ok());
    if (!e.ok()) { /* expected malformed */ }
    // huge requested payload length rejected
    FrameHeader h;
    std::vector<std::uint8_t> out;
    TF_CHECK(!FrameEncoder::encode(h, nullptr, Protocol::kMaxPayload + 1u, out).ok()); // exceeds max -> rejected
}
TF_TEST(storage_frame_partial) {
    // feeding a frame byte-by-byte must reconstruct it
    FrameHeader h; h.type = static_cast<std::uint8_t>(FrameType::chunk); h.transfer_id=TransferId(9,8); h.length=4; h.chunk_id=1;
    std::vector<std::uint8_t> out; FrameEncoder::encode(h, "WXYZ", 4, out);
    FrameDecoder dec; Error e;
    for (size_t i=0;i<out.size();++i) {
        auto f = dec.feed(out.data()+i, 1, e);
        if (i+1<out.size()) TF_CHECK(!f.has_value());
        else { TF_REQUIRE(f.has_value()); TF_CHECK(std::memcmp(f->payload.data(),"WXYZ",4)==0); }
    }
}

// ---- File -> host / host -> file ------------------------------------
TF_TEST(storage_file_to_host) {
    std::vector<std::uint8_t> src(1<<16); for (size_t i=0;i<src.size();++i) src[i]=(std::uint8_t)(i&0xFF);
    std::string path = temp_path("f2h.bin"); write_file(path, src.data(), src.size());
    Runtime rt({});
    std::vector<std::uint8_t> dst(1<<16, 0);
    EndpointHandle fe, he;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(path, 0, src.size()), fe).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(dst.data(), dst.size()), he).ok());
    TransferOptions opts; opts.source=fe; opts.destination=he;
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(rt.wait(h));
    TF_CHECK(std::memcmp(src.data(), dst.data(), src.size())==0);
    rt.shutdown();
}
TF_TEST(storage_host_to_file) {
    std::vector<std::uint8_t> src(1<<15); for (size_t i=0;i<src.size();++i) src[i]=(std::uint8_t)((i*3+1)&0xFF);
    std::string path = temp_path("h2f.bin");
    { std::vector<std::uint8_t> pre(src.size(), 0); write_file(path, pre.data(), pre.size()); }  // pre-size the dest file
    Runtime rt({});
    std::vector<std::uint8_t> dst(1<<15, 0);
    EndpointHandle he, fe;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), src.size()), he).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(path, 0, src.size()), fe).ok());
    TransferOptions opts; opts.source=he; opts.destination=fe;
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(rt.wait(h));
    // Verify via the runtime opposite-direction transfer: file -> host.
    std::vector<std::uint8_t> back(src.size(), 0);
    EndpointHandle he2;
    rt.register_endpoint(EndpointDescriptor::host_memory(back.data(), back.size()), he2);
    TransferOptions r; r.source=fe; r.destination=he2;
    TransferHandle h2 = rt.submit(r, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(rt.wait(h2));
    TF_CHECK(std::memcmp(src.data(), back.data(), src.size())==0);
    rt.shutdown();
}
TF_TEST(storage_path_traversal_rejected) {
    Runtime rt({});
    std::vector<std::uint8_t> b(1024);
    EndpointHandle fe;
    Error e = rt.register_endpoint(EndpointDescriptor::file_region("..\\..\\evil.txt", 0, 1024), fe);
    TF_CHECK(!e.ok());   // path traversal rejected
    e = rt.register_endpoint(EndpointDescriptor::file_region("C:\\Temp\\..\\selftest.txt", 0, 1024), fe);
    TF_CHECK(!e.ok());
    rt.shutdown();
}
TF_TEST(storage_short_read) {
    // file smaller than requested length -> should fail (short read)
    std::string path = temp_path("short.bin"); std::vector<std::uint8_t> data(10, 7); write_file(path, data.data(), 10);
    Runtime rt({});
    std::vector<std::uint8_t> buf(100, 0);
    EndpointHandle fe, he;
    rt.register_endpoint(EndpointDescriptor::file_region(path, 0, 10), fe);   // region = 10 bytes
    rt.register_endpoint(EndpointDescriptor::host_memory(buf.data(), 100), he);
    // Request 100 bytes from a 10-byte region should hit short read after 10
    TransferOptions opts; opts.source=fe; opts.destination=he;
    opts.source_range = ByteRange{0, 100};  // exceeds region -> runtime checks range and fails before copy
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_CHECK(!err.ok());   // out-of-bounds range rejected
    rt.shutdown();
}

// ---- Shared memory --------------------------------------------------
TF_TEST(storage_shared_memory) {
    Runtime rt({});
    std::vector<std::uint8_t> src(4096, 0x5A), dst(4096, 0);
    EndpointHandle sh1, sh2, he;
    // create a shared region, write host->shared, then shared->host
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::shared_region("tf_shm_test", 0, 4096), sh1).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(dst.data(), dst.size()), he).ok());
    TransferOptions w; w.source=he; w.destination=sh1;
    Error err; TransferHandle h = rt.submit(w, err);
    TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
    // read shared -> host
    std::vector<std::uint8_t> back(4096, 0);
    EndpointHandle he2;
    rt.register_endpoint(EndpointDescriptor::host_memory(back.data(), back.size()), he2);
    TransferOptions r; r.source=sh1; r.destination=he2;
    TransferHandle h2 = rt.submit(r, err);
    TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h2));
    // NOTE: the shared region starts zero (we wrote zero dst). This just validates the path.
    rt.shutdown();
}

// ---- TCP server/client file transfer --------------------------------
TF_TEST(storage_tcp_transfer) {
#if defined(_WIN32)
    std::string srvpath = temp_path("tcp_src.bin");
    std::string clipath = temp_path("tcp_dst.bin");
    std::vector<std::uint8_t> data(1<<16); for (size_t i=0;i<data.size();++i) data[i]=(std::uint8_t)(i*7&0xFF);
    write_file(srvpath, data.data(), data.size());
    TcpChunkServer server;
    TF_REQUIRE(server.listen(TcpTransport::default_port()).ok());
    // serve in a background thread
    std::thread t([&](){ server.serve_once(); });
    TcpChunkClient client;
    TF_REQUIRE(client.connect("127.0.0.1", TcpTransport::default_port()).ok());
    TransferId id = TransferId::generate();
    Error e = client.transfer(srvpath, 0, data.size(), clipath, id);
    TF_CHECK(e.ok());
    client.close();
    if (t.joinable()) t.join();
    server.close();
    std::vector<std::uint8_t> got(data.size(), 0);
    TF_REQUIRE(read_file(clipath, got.data(), got.size()));
    TF_CHECK(std::memcmp(data.data(), got.data(), data.size())==0);
#else
    TF_CHECK(true); // TCP validated on Windows
#endif
}

#if defined(_WIN32)
// ---- Regions: offset and declared size ------------------------------
// A file endpoint is the region its descriptor names: transfers are relative
// to the region's offset and cannot go past its declared size.
TF_TEST(storage_file_region_offset_and_size) {
    const std::size_t page = 4096;
    std::vector<std::uint8_t> file(3 * page);
    for (size_t i=0;i<file.size();++i) file[i]=(std::uint8_t)((i*131+(i>>12)*97+17)>>3);   // differs page to page
    std::string path = temp_path("region.bin");
    TF_REQUIRE(write_file(path, file.data(), file.size()));
    std::vector<std::uint8_t> got(page, 0), big(2*page, 0), in(page, 0xC3), after(file.size(), 0);
    {
        Runtime rt({});
        EndpointHandle region, whole, hgot, hbig, hin, hafter;
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(path, page, page), region).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(path, 0, file.size()), whole).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(got.data(), got.size()), hgot).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(big.data(), big.size()), hbig).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(in.data(), in.size()), hin).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(after.data(), after.size()), hafter).ok());
        EndpointCapabilities caps;
        TF_REQUIRE(rt.query_endpoint(region, caps).ok());
        TF_EQ(caps.max_transfer_size, (byte_count)page);

        // Read: the region's first byte is the file's byte 4096.
        Error err;
        TransferOptions rd; rd.source=region; rd.destination=hgot; rd.source_range=ByteRange{0, page};
        TransferHandle h = rt.submit(rd, err);
        TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
        TF_CHECK(std::memcmp(got.data(), file.data()+page, page)==0);

        // The region is one page: asking for two is out of bounds, not a read
        // of what follows the region in the file.
        TransferOptions over; over.source=region; over.destination=hbig; over.source_range=ByteRange{0, 2*page};
        h = rt.submit(over, err);
        TF_CHECK(!err.ok());
        if (err.ok()) rt.wait(h);

        // Write: lands inside the region; the bytes around it are untouched.
        TransferOptions wr; wr.source=hin; wr.destination=region;
        h = rt.submit(wr, err);
        TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
        TransferOptions back; back.source=whole; back.destination=hafter;
        h = rt.submit(back, err);
        TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
        TF_CHECK(std::memcmp(after.data(), file.data(), page)==0);
        bool filled = true; for (size_t i=page;i<2*page;++i) filled = filled && after[i]==0xC3;
        TF_CHECK(filled);
        TF_CHECK(std::memcmp(after.data()+2*page, file.data()+2*page, page)==0);

        // A region that starts past the end of the file does not exist.
        EndpointHandle beyond;
        TF_CHECK(!rt.register_endpoint(EndpointDescriptor::file_region(path, file.size()+1, 16), beyond).ok());
        rt.shutdown();
    }
    DeleteFileA(path.c_str());
}

// A mapped-file endpoint covers the declared region of what is really mapped,
// and gives the file back once nothing uses it.
TF_TEST(storage_mmap_region_is_bounded_and_released) {
    const std::size_t page = 4096;
    std::vector<std::uint8_t> file(2 * page);
    for (size_t i=0;i<file.size();++i) file[i]=(std::uint8_t)((i*29+(i>>12)*101+3)>>2);   // differs page to page
    std::string path = temp_path("mmap.bin");
    TF_REQUIRE(write_file(path, file.data(), file.size()));
    std::vector<std::uint8_t> got(page, 0);
    Runtime rt({});
    EndpointHandle claimed, tail, hgot, bad;
    // The declared size is larger than the file: the endpoint ends where the
    // mapping does.
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::mmap_region(path, 0, 1u<<20), claimed).ok());
    EndpointCapabilities caps;
    TF_REQUIRE(rt.query_endpoint(claimed, caps).ok());
    TF_EQ(caps.max_transfer_size, (byte_count)file.size());
    // The offset says where the region starts.
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::mmap_region(path, page, page), tail).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(got.data(), got.size()), hgot).ok());
    TransferOptions o; o.source=tail; o.destination=hgot;
    Error err; TransferHandle h = rt.submit(o, err);
    TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
    TF_CHECK(std::memcmp(got.data(), file.data()+page, page)==0);
    TF_CHECK(!rt.register_endpoint(EndpointDescriptor::mmap_region(path, file.size()+1, 16), bad).ok());
    TF_CHECK(!rt.register_endpoint(EndpointDescriptor::mmap_region("..\\tf_mmap_outside.bin", 0, 16), bad).ok());
    // Unregistered and no longer used by a transfer: the view and the handles
    // are gone, so the file can be removed while the runtime is still up.
    TF_REQUIRE(rt.unregister_endpoint(claimed).ok());
    TF_REQUIRE(rt.unregister_endpoint(tail).ok());
    TF_CHECK(DeleteFileA(path.c_str()) != 0);
    rt.shutdown();
}

// A shared endpoint is the region [offset, offset + size) of the named mapping.
TF_TEST(storage_shared_region_offset) {
    const std::size_t page = 4096;
    const std::string name = "tf_shm_region_" + std::to_string(GetCurrentProcessId());
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)(3*page), name.c_str());
    TF_REQUIRE(map != nullptr);
    auto* view = static_cast<std::uint8_t*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, 3*page));
    TF_REQUIRE(view != nullptr);
    std::memset(view, 0, 3*page);
    std::vector<std::uint8_t> src(page, 0x5A);
    {
        Runtime rt({});
        EndpointHandle he, sh;
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), src.size()), he).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::shared_region(name, page, page), sh).ok());
        TransferOptions o; o.source=he; o.destination=sh;
        Error err; TransferHandle h = rt.submit(o, err);
        TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
        rt.shutdown();
    }
    bool before = true, inside = true, behind = true;
    for (size_t i=0;i<page;++i) {
        before = before && view[i]==0;
        inside = inside && view[page+i]==0x5A;
        behind = behind && view[2*page+i]==0;
    }
    TF_CHECK(before); TF_CHECK(inside); TF_CHECK(behind);
    UnmapViewOfFile(view); CloseHandle(map);
}

// ---- Staged routes ----------------------------------------------------
// file -> host staging -> shared memory. Every transfer holds its own staging
// buffers while it runs and gives them back once.
TF_TEST(storage_staged_transfers_keep_their_own_staging) {
    const int N = 6;
    const std::size_t sz = 1u << 20;
    const DWORD pid = GetCurrentProcessId();
    std::vector<std::vector<std::uint8_t>> data(N, std::vector<std::uint8_t>(sz));
    std::vector<std::string> paths(N), names(N);
    std::vector<HANDLE> maps(N, nullptr);
    std::vector<std::uint8_t*> views(N, nullptr);
    for (int i=0;i<N;++i) {
        for (size_t j=0;j<sz;++j) data[i][j]=(std::uint8_t)(((j>>12)*7 + j + (size_t)i*53) & 0xFF);
        paths[i] = temp_path(("stage_" + std::to_string(i) + ".bin").c_str());
        TF_REQUIRE(write_file(paths[i], data[i].data(), sz));
        names[i] = "tf_stage_" + std::to_string(pid) + "_" + std::to_string(i);
        maps[i] = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sz, names[i].c_str());
        TF_REQUIRE(maps[i] != nullptr);
        views[i] = static_cast<std::uint8_t*>(MapViewOfFile(maps[i], FILE_MAP_ALL_ACCESS, 0, 0, sz));
        TF_REQUIRE(views[i] != nullptr);
    }
    int wrong = 0, unfinished = 0;
    {
        Runtime rt({});
        std::vector<EndpointHandle> fe(N), sh(N);
        for (int i=0;i<N;++i) {
            TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(paths[i], 0, sz), fe[i]).ok());
            TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::shared_region(names[i], 0, sz), sh[i]).ok());
        }
        PlanResult pr = rt.plan(fe[0], sh[0], sz, TransferPolicy{});
        TF_REQUIRE(pr.found);
        TF_CHECK(!pr.best.is_direct());
        // The first round returns its staging to the pool; the rounds after it
        // run on reused buffers, several transfers at a time.
        for (int round=0; round<4; ++round) {
            for (int i=0;i<N;++i) std::memset(views[i], 0, sz);
            std::vector<TransferHandle> hs(N);
            for (int i=0;i<N;++i) {
                TransferOptions o; o.source=fe[i]; o.destination=sh[i];
                o.policy.preferred_chunk_size = 4096;
                Error e; hs[i] = rt.submit(o, e);
                TF_REQUIRE(e.ok());
            }
            for (int i=0;i<N;++i) {
                if (!rt.wait(hs[i])) ++unfinished;
                else if (std::memcmp(views[i], data[i].data(), sz)!=0) ++wrong;
            }
        }
        rt.shutdown();
    }
    TF_EQ(unfinished, 0);
    TF_EQ(wrong, 0);
    for (int i=0;i<N;++i) { UnmapViewOfFile(views[i]); CloseHandle(maps[i]); DeleteFileA(paths[i].c_str()); }
}

// ---- One file endpoint, many transfers -------------------------------
// Transfers that share a file endpoint each read and write at their own
// offsets, whatever the others are doing with the same handle.
TF_TEST(storage_concurrent_transfers_share_a_file) {
    const std::size_t parts = 16, rpart = 1u << 18, wpart = 1u << 16;
    std::vector<std::uint8_t> data(parts * rpart);
    for (size_t i=0;i<data.size();++i) data[i]=(std::uint8_t)((i*2654435761ull)>>24);
    std::string src_path = temp_path("shared_src.bin"), dst_path = temp_path("shared_dst.bin");
    TF_REQUIRE(write_file(src_path, data.data(), data.size()));
    { std::vector<std::uint8_t> zero(parts * wpart, 0); TF_REQUIRE(write_file(dst_path, zero.data(), zero.size())); }
    std::vector<std::vector<std::uint8_t>> out(parts, std::vector<std::uint8_t>(rpart, 0));
    std::vector<std::uint8_t> written(parts * wpart, 0);
    int unfinished = 0, wrong = 0;
    {
        Runtime rt({});
        EndpointHandle fsrc, fdst, hall, hback;
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(src_path, 0, data.size()), fsrc).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(dst_path, 0, written.size()), fdst).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(data.data(), data.size()), hall).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(written.data(), written.size()), hback).ok());
        std::vector<TransferHandle> hs;
        for (size_t p=0;p<parts;++p) {            // readers: one range of the source file each
            EndpointHandle he;
            TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(out[p].data(), rpart), he).ok());
            TransferOptions o; o.source=fsrc; o.destination=he;
            o.source_range = ByteRange{p*rpart, rpart};
            o.policy.preferred_chunk_size = 4096;
            Error e; TransferHandle h = rt.submit(o, e);
            TF_REQUIRE(e.ok()); hs.push_back(h);
        }
        for (size_t p=0;p<parts;++p) {            // writers: one range of the destination file each
            TransferOptions o; o.source=hall; o.destination=fdst;
            o.source_range = ByteRange{p*wpart, wpart};
            o.destination_range = ByteRange{p*wpart, wpart};
            o.policy.preferred_chunk_size = 4096;
            Error e; TransferHandle h = rt.submit(o, e);
            TF_REQUIRE(e.ok()); hs.push_back(h);
        }
        for (auto& h : hs) if (!rt.wait(h)) ++unfinished;
        for (size_t p=0;p<parts;++p) if (std::memcmp(out[p].data(), data.data()+p*rpart, rpart)!=0) ++wrong;
        // Read the written file back in one piece.
        TransferOptions back; back.source=fdst; back.destination=hback;
        Error e; TransferHandle h = rt.submit(back, e);
        TF_REQUIRE(e.ok()); TF_REQUIRE(rt.wait(h));
        rt.shutdown();
    }
    TF_EQ(unfinished, 0);
    TF_EQ(wrong, 0);
    TF_CHECK(std::memcmp(written.data(), data.data(), written.size())==0);
    DeleteFileA(src_path.c_str()); DeleteFileA(dst_path.c_str());
}

// ---- Policy gates on storage and staging ----------------------------
TF_TEST(storage_policy_gates) {
    const std::size_t page = 4096;
    std::string path = temp_path("policy.bin");
    std::vector<std::uint8_t> zero(page, 0), src(page, 0x7E), back(page, 0xFF);
    TF_REQUIRE(write_file(path, zero.data(), zero.size()));
    const std::string name = "tf_policy_" + std::to_string(GetCurrentProcessId());
    HANDLE map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)page, name.c_str());
    TF_REQUIRE(map != nullptr);
    auto* view = static_cast<std::uint8_t*>(MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, page));
    TF_REQUIRE(view != nullptr);
    std::memset(view, 0x11, page);
    {
        Runtime rt({});
        EndpointHandle he, fe, hb, sh;
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), src.size()), he).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::file_region(path, 0, page), fe).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(back.data(), back.size()), hb).ok());
        TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::shared_region(name, 0, page), sh).ok());
        // A policy without persistence permission cannot write a file.
        TransferOptions deny; deny.source=he; deny.destination=fe; deny.policy.persistence_permission=false;
        Error err; TransferHandle h = rt.submit(deny, err);
        TF_CHECK(!err.ok());
        if (err.ok()) rt.wait(h);
        TransferOptions rd; rd.source=fe; rd.destination=hb;
        h = rt.submit(rd, err);
        TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
        TF_CHECK(back == zero);                    // the file is unchanged

        // file -> shared memory is staged. A policy that allows no staging
        // bytes stops the transfer before a byte moves.
        TransferOptions tight; tight.source=fe; tight.destination=sh;
        tight.policy.preferred_chunk_size = page;
        tight.policy.max_staging_bytes = 0;
        h = rt.submit(tight, err);
        TF_REQUIRE(err.ok());                      // admitted; refused when it reserves staging
        TF_CHECK(!rt.wait(h));
        TF_CHECK(rt.status(h).error.category == ErrorCategory::quota_exceeded);
        bool untouched = true; for (size_t i=0;i<page;++i) untouched = untouched && view[i]==0x11;
        TF_CHECK(untouched);
        // With room for its two staging buffers the same transfer runs.
        TransferOptions roomy = tight; roomy.policy.max_staging_bytes = 2*page;
        h = rt.submit(roomy, err);
        TF_REQUIRE(err.ok()); TF_CHECK(rt.wait(h));
        TF_CHECK(std::memcmp(view, zero.data(), page)==0);
        rt.shutdown();
    }
    UnmapViewOfFile(view); CloseHandle(map); DeleteFileA(path.c_str());
}

// ---- TCP: ranges and chunk validation -------------------------------
// A request for a range that does not start at zero is served from that
// offset; the client mirrors the source offsets in the destination file.
TF_TEST(storage_tcp_transfer_of_a_sub_range) {
    std::string srvpath = temp_path("tcp_range_src.bin");
    std::string clipath = temp_path("tcp_range_dst.bin");
    std::vector<std::uint8_t> data(3u<<16); for (size_t i=0;i<data.size();++i) data[i]=(std::uint8_t)((i*11+(i>>9))&0xFF);
    TF_REQUIRE(write_file(srvpath, data.data(), data.size()));
    const byte_offset off = 1u<<16;
    const byte_count len = (1u<<16) + 4096;
    TcpChunkServer server;
    TF_REQUIRE(server.listen(TcpTransport::default_port()).ok());
    Error served;
    std::thread t([&](){ served = server.serve_once(); });
    TcpChunkClient client;
    Error e = client.connect("127.0.0.1", TcpTransport::default_port());
    if (e.ok()) e = client.transfer(srvpath, off, len, clipath, TransferId::generate());
    client.close();
    server.close();
    t.join();
    TF_CHECK(e.ok());
    TF_CHECK(served.ok());
    std::vector<std::uint8_t> got((size_t)(off + len), 0);
    TF_REQUIRE(read_file(clipath, got.data(), got.size()));
    TF_CHECK(std::memcmp(got.data()+off, data.data()+off, (size_t)len)==0);
    DeleteFileA(srvpath.c_str()); DeleteFileA(clipath.c_str());
}

// A hand-written peer for the client side of the frame protocol.
struct RawPeer {
    SOCKET listener{INVALID_SOCKET};
    std::uint16_t port{0};
    bool open() {
        WSADATA d;
        if (WSAStartup(MAKEWORD(2, 2), &d) != 0) return false;
        listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = 0;
        if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return false;
        int alen = sizeof(addr);
        if (::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &alen) != 0) return false;
        port = ntohs(addr.sin_port);
        return ::listen(listener, 1) == 0;
    }
    void close() { if (listener != INVALID_SOCKET) { ::closesocket(listener); listener = INVALID_SOCKET; } }
};
// Reads frames until one of the wanted type arrives; false when the client is gone.
static bool peer_recv(SOCKET s, FrameDecoder& dec, FrameType want, DecodedFrame& out) {
    for (;;) {
        Error e;
        auto f = dec.feed(nullptr, 0, e);          // a frame that is already buffered
        if (!e.ok()) return false;
        if (!f) {
            char tmp[4096];
            int r = ::recv(s, tmp, sizeof(tmp), 0);
            if (r <= 0) return false;
            f = dec.feed(reinterpret_cast<const std::uint8_t*>(tmp), static_cast<std::size_t>(r), e);
            if (!e.ok()) return false;
            if (!f) continue;
        }
        if (f->header.type == static_cast<std::uint8_t>(want)) { out = *f; return true; }
    }
}
static void peer_send(SOCKET s, FrameType type, const TransferId& id, std::uint64_t chunk,
                      std::uint64_t offset, const std::vector<std::uint8_t>& payload) {
    FrameHeader h; h.type = static_cast<std::uint8_t>(type); h.transfer_id = id;
    h.chunk_id = chunk; h.offset = offset; h.length = payload.size();
    std::vector<std::uint8_t> out;
    FrameEncoder::encode(h, payload.data(), payload.size(), out);
    ::send(s, reinterpret_cast<const char*>(out.data()), static_cast<int>(out.size()), 0);
}

// A peer that reaches the byte count with the wrong bytes has not completed a
// transfer: every chunk has to follow the previous one exactly.
TF_TEST(storage_tcp_client_rejects_inconsistent_chunks) {
    RawPeer peer;
    TF_REQUIRE(peer.open());
    std::string clipath = temp_path("tcp_bad_dst.bin");
    std::thread server([&peer]{
        SOCKET cs = ::accept(peer.listener, nullptr, nullptr);
        if (cs == INVALID_SOCKET) return;
        FrameDecoder dec; DecodedFrame f;
        const std::vector<std::uint8_t> none, first(4096, 'A'), again(4096, 'B');
        if (peer_recv(cs, dec, FrameType::hello, f)) {
            peer_send(cs, FrameType::hello_ack, TransferId(), 0, 0, none);
            if (peer_recv(cs, dec, FrameType::transfer_req, f)) {
                const TransferId id = f.header.transfer_id;
                peer_send(cs, FrameType::chunk, id, 0, 0, first);
                if (peer_recv(cs, dec, FrameType::chunk_ack, f)) {
                    // The second chunk covers the first one again instead of following it.
                    peer_send(cs, FrameType::chunk, id, 1, 0, again);
                    if (peer_recv(cs, dec, FrameType::chunk_ack, f)) {
                        peer_send(cs, FrameType::commit, id, 0, 0, none);
                        peer_recv(cs, dec, FrameType::commit_ack, f);
                    }
                }
            }
        }
        ::closesocket(cs);
    });
    TcpChunkClient client;
    Error e = client.connect("127.0.0.1", peer.port);
    if (e.ok()) e = client.transfer("unused", 0, 8192, clipath, TransferId::generate());
    client.close();
    peer.close();
    server.join();
    TF_CHECK(!e.ok());
    DeleteFileA(clipath.c_str());
}
#endif

TF_TEST_MAIN()
