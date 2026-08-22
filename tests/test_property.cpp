#include "tf_test.hpp"
#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/telemetry.hpp"

#include <vector>
#include <cstring>
#include <cstdio>
#include <random>
#include <string>

#if defined(_WIN32)
#  include <windows.h>
#endif

using namespace transfer_fabric;

// Deterministic seeded RNG so failures are reproducible.
TF_TEST(property_randomized_ops) {
    const std::uint64_t seed = 0x123456789ABCDEF0ull;
    std::mt19937_64 rng(seed);
    auto uniform = [&](std::uint64_t lo, std::uint64_t hi) -> std::uint64_t {
        return lo + (rng() % (hi - lo + 1));
    };

    Runtime rt({});
    const std::size_t cap = 1u << 14;
    std::vector<std::uint8_t> a(cap), b(cap, 0);
    for (size_t i=0;i<cap;++i) a[i] = (std::uint8_t)(i & 0xFF);
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), cap), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), cap), de);

    const int N = 2000;
    int completed = 0, cancelled = 0, failed = 0;
    for (int i = 0; i < N; ++i) {
        // Random size (0..cap), offset (0..cap), chunk size, priority.
        std::uint64_t len = uniform(0, (std::uint64_t)cap);
        std::uint64_t off = uniform(0, (std::uint64_t)(cap - len));
        byte_count chunk = uniform(64, 4096);
        auto pri = (Priority)(int)uniform(0, 6);
        TransferOptions o;
        o.source = se; o.destination = de;
        o.source_range = ByteRange{off, len};
        o.destination_range = ByteRange{off, len};
        o.policy.preferred_chunk_size = chunk;
        o.policy.queue_priority = (std::uint32_t)pri;
        o.priority = pri;
        Error err;
        TransferHandle h = rt.submit(o, err);
        if (!err.ok()) { failed++; continue; }        // e.g. out-of-range rejected
        // Random cancellation.
        if (uniform(0, 3) == 0) rt.cancel(h);
        rt.wait(h);   // must reach terminal, never hang
        auto st = rt.status(h);
        if (st.state == TransferState::completed) completed++;
        else if (st.state == TransferState::cancelled) cancelled++;
        else failed++;
    }
    TF_CHECK(completed + cancelled + failed == N);
    // Property: no leaked/active/queued work, accounting closed.
    auto s = rt.telemetry();
    TF_EQ(s.transfers_active, 0u);
    TF_EQ(s.staging_usage, 0u);
    TF_EQ(s.queue_depth, 0u);
    std::printf("  seed=0x%016llx completed=%d cancelled=%d rejected=%d\n",
                (unsigned long long)seed, completed, cancelled, failed);
    rt.shutdown();
}

// Failure injection: invalid ranges, oversized claims, cancellation races,
// stale handles, and short-file reads must all reach a legal terminal or error,
// and never leak accounting.
TF_TEST(property_failure_injection) {
    std::mt19937_64 rng(0xDEADBEEFCAFEBABEull);
    auto uniform = [&](std::uint64_t lo, std::uint64_t hi) -> std::uint64_t {
        return lo + (rng() % (hi - lo + 1));
    };
    Runtime rt({});
    const std::size_t cap = 1u << 12;
    std::vector<std::uint8_t> a(cap), b(cap, 0);
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), cap), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), cap), de);

    // 1. Invalid ranges are rejected, not silently truncated.
    {
        TransferOptions o; o.source=se; o.destination=de;
        o.source_range = ByteRange{cap, 1u<<20};   // offset == size -> out of bounds
        Error e; rt.submit(o, e);
        TF_CHECK(!e.ok());
    }
    // 2. Overflow offset+length.
    {
        TransferOptions o; o.source=se; o.destination=de;
        o.source_range = ByteRange{(byte_offset)~0ull - 1, (byte_count)~0ull};
        Error e; rt.submit(o, e);
        TF_CHECK(!e.ok());
    }
    // 3. Cancellation racing: submit + cancel from the same thread many times.
    for (int i=0;i<64;++i) {
        TransferOptions o; o.source=se; o.destination=de;
        byte_count len = (byte_count)uniform(0, cap);
        o.source_range = ByteRange{0, len};
        Error e; TransferHandle h = rt.submit(o, e);
        if (!e.ok()) continue;
        rt.cancel(h);
        rt.wait(h);   // never hangs
        auto st = rt.status(h);
        TF_CHECK(is_terminal(st.state));
    }
    // 4. Stale endpoint handle rejected.
    EndpointHandle e2;
    if (rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), cap), e2).ok()) {
        rt.unregister_endpoint(e2);
        TransferOptions o; o.source=e2; o.destination=de;
        Error e; rt.submit(o, e);
        TF_CHECK(!e.ok());
    }
    // 5. Short file read -> hard failure (file smaller than requested region).
#if defined(_WIN32)
    {
        std::string P = "C:\\Temp\\tf_prop_short.bin";
        HANDLE h = CreateFileA(P.c_str(), GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (h != INVALID_HANDLE_VALUE) { std::uint8_t z=0; DWORD w=0; WriteFile(h,&z,1,&w,nullptr); CloseHandle(h); }
        std::vector<std::uint8_t> buf(4096, 0);
        EndpointHandle fe, he;
        rt.register_endpoint(EndpointDescriptor::file_region(P, 0, 1), fe);   // region claims 1 byte
        rt.register_endpoint(EndpointDescriptor::host_memory(buf.data(), buf.size()), he);
        TransferOptions o; o.source=fe; o.destination=he;
        o.source_range = ByteRange{0, 4096};   // request more than the 1-byte region
        Error e; rt.submit(o, e);
        TF_CHECK(!e.ok());   // out-of-bounds or short read rejected
    }
#endif
    auto s = rt.telemetry();
    TF_EQ(s.transfers_active, 0u);
    rt.shutdown();
}

TF_TEST_MAIN()
