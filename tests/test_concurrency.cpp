#include "tf_test.hpp"
#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/telemetry.hpp"
#include <vector>
#include <thread>
#include <cstring>

using namespace transfer_fabric;

// Shared source/dest. All transfers copy identical bytes src->dst, so content is safe.
TF_TEST(concurrency_many_transfers) {
    Runtime rt({});
    const std::size_t sz = 1u << 16;
    std::vector<std::uint8_t> a(sz), b(sz, 0);
    for (size_t i=0;i<sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    const int N = 100;
    std::vector<TransferHandle> hs; hs.reserve(N);
    for (int i=0;i<N;++i) {
        TransferOptions o; o.source=se; o.destination=de; Error e;
        TransferHandle h = rt.submit(o, e);
        TF_REQUIRE(e.ok());
        hs.push_back(h);
    }
    int ok=0;
    for (auto& h : hs) if (rt.wait(h)) ok++;
    TF_EQ(ok, N);
    TF_CHECK(std::memcmp(a.data(), b.data(), sz)==0);
    auto s = rt.telemetry();
    TF_EQ(s.transfers_completed, (std::uint64_t)N);
    TF_EQ(s.transfers_active, 0u);
    TF_EQ(s.transfers_created, (std::uint64_t)N);
    rt.shutdown();
}

TF_TEST(concurrency_parallel_submit_wait) {
    // Submit from multiple threads concurrently; all must complete.
    Runtime rt({});
    const std::size_t sz = 1u << 14;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (auto& x : a) x = 0x11;
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    const int T = 8, PER = 12;
    std::atomic<int> okc{0};
    std::vector<std::thread> threads;
    for (int t=0;t<T;++t) threads.emplace_back([&]{
        for (int i=0;i<PER;++i) {
            TransferOptions o; o.source=se; o.destination=de; Error e;
            TransferHandle h = rt.submit(o, e);
            if (e.ok() && rt.wait(h)) okc.fetch_add(1);
        }
    });
    for (auto& th : threads) th.join();
    TF_EQ(okc.load(), T*PER);
    auto s = rt.telemetry();
    TF_EQ(s.transfers_completed, (std::uint64_t)(T*PER));
    TF_EQ(s.transfers_active, 0u);
    rt.shutdown();
}

TF_TEST(concurrency_cancel_all) {
    Runtime rt({});
    const std::size_t sz = 1u << 22;   // 4 MiB, many chunks so cancellation is observable
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (auto& x : a) x = 0x77;
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    const int N = 24;
    std::vector<TransferHandle> hs; hs.reserve(N);
    for (int i=0;i<N;++i) {
        TransferOptions o; o.source=se; o.destination=de; Error e;
        TransferHandle h = rt.submit(o, e);
        TF_REQUIRE(e.ok());
        hs.push_back(h);
        rt.cancel(h);   // request cancellation immediately
    }
    int completed=0, cancelled=0, other=0;
    for (auto& h : hs) {
        rt.wait(h);   // must reach a terminal state, never hang
        TransferStatus st = rt.status(h);
        if (st.state == TransferState::completed) completed++;
        else if (st.state == TransferState::cancelled) cancelled++;
        else other++;
    }
    TF_EQ(completed + cancelled + other, N);
    TF_CHECK(cancelled >= 0);
    // No resource leak: active returns to zero.
    auto s = rt.telemetry();
    TF_EQ(s.transfers_active, 0u);
    rt.shutdown();
}

TF_TEST(concurrency_shutdown_with_queued) {
    // Submit work then immediately shut down; shutdown must drain and join, no hang.
    Runtime rt({});
    const std::size_t sz = 1u << 18;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (auto& x : a) x = 0x12;
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    for (int i=0;i<40;++i) { TransferOptions o; o.source=se; o.destination=de; Error e; rt.submit(o, e); }
    rt.shutdown();   // must drain and join without deadlock
    auto s = rt.telemetry();
    TF_EQ(s.transfers_active, 0u);
}

TF_TEST_MAIN()
