#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/telemetry.hpp"
#include "transfer_fabric/platform/cuda_detect.hpp"
#include <cstdint>
#include <cstdio>
#include <vector>
#include <cstring>
#include <chrono>
#include <windows.h>
#include <cuda_runtime.h>

using namespace transfer_fabric;
using clock_type = std::chrono::steady_clock;
static std::uint64_t now_ns2() { return (std::uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(clock_type::now().time_since_epoch()).count(); }
static double gbps(std::uint64_t bytes, std::uint64_t nsec) { return nsec ? (double(bytes)/1e9)/(double(nsec)/1e9) : 0.0; }

static void bench_host_to_host(std::uint64_t sz) {
    std::vector<std::uint8_t> a((size_t)sz), b((size_t)sz,0);
    for (size_t i=0;i<(size_t)sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    Runtime rt({}); EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    // warm-up
    for (int r=0;r<8;++r) { TransferOptions o; o.source=se; o.destination=de; Error e;
        TransferHandle h = rt.submit(o,e); if (e.ok()) rt.wait(h); }
    std::uint64_t t0=now_ns2();
    std::vector<TransferHandle> hs; hs.reserve(8);
    for (int r=0;r<8;++r) { TransferOptions o; o.source=se; o.destination=de; Error e;
        TransferHandle h = rt.submit(o,e); hs.push_back(h); }
    for (auto& h : hs) rt.wait(h);          // measure to completion, not just submit
    std::uint64_t t1=now_ns2();
    bool ok = std::memcmp(a.data(), b.data(), (size_t)sz)==0;
    std::printf("host->host  %8llu B   %8.3f GiB/s   ok=%d\n", (unsigned long long)sz, gbps(sz*8, t1-t0), ok);
    rt.shutdown();
}

static void bench_file(std::uint64_t sz) {
    std::string P="C:\\Temp\\tf_bench.bin"; std::vector<std::uint8_t> a((size_t)sz), b((size_t)sz,0);
    for (size_t i=0;i<(size_t)sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    { HANDLE h=CreateFileA(P.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD w=0; WriteFile(h,a.data(),(DWORD)sz,&w,nullptr); CloseHandle(h); }
    Runtime rt({}); EndpointHandle fe, he;
    rt.register_endpoint(EndpointDescriptor::file_region(P, 0, sz), fe);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), he);
    std::uint64_t t0=now_ns2(); TransferOptions o; o.source=fe; o.destination=he; Error e; TransferHandle h=rt.submit(o,e); rt.wait(h); std::uint64_t t1=now_ns2();
    bool ok = std::memcmp(a.data(), b.data(), (size_t)sz)==0;
    std::printf("file->host %8llu B   %8.3f GiB/s   ok=%d\n", (unsigned long long)sz, gbps(sz, t1-t0), ok);
    rt.shutdown();
}

static void bench_cuda(std::uint64_t sz) {
    if (!cuda_available()) { std::printf("cuda: unavailable\n"); return; }
    std::vector<std::uint8_t> a((size_t)sz); for (size_t i=0;i<(size_t)sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    void* dev=nullptr; cudaMalloc(&dev, sz);
    Runtime rt({}); EndpointHandle he, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), he);
    rt.register_endpoint(EndpointDescriptor::device_memory(0, dev, sz), de);
    std::uint64_t t0=now_ns2(); TransferOptions o; o.source=he; o.destination=de; Error e; TransferHandle h=rt.submit(o,e); rt.wait(h); std::uint64_t t1=now_ns2();
    std::vector<std::uint8_t> rb((size_t)sz,0); cudaMemcpy(rb.data(), dev, sz, cudaMemcpyDeviceToHost); cudaDeviceSynchronize();
    bool ok = std::memcmp(a.data(), rb.data(), (size_t)sz)==0;
    std::printf("pageable H2D %8llu B   %8.3f GiB/s   ok=%d\n", (unsigned long long)sz, gbps(sz, t1-t0), ok);
    rt.shutdown(); cudaFree(dev);
}

static void bench_planner(std::uint64_t sz) {
    Runtime rt({}); std::vector<std::uint8_t> a((size_t)sz), b((size_t)sz,0);
    EndpointHandle se, de; rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    std::uint64_t t0=now_ns2(); for (int i=0;i<1000;++i) { TransferPolicy p; rt.plan(se, de, sz, p); } std::uint64_t t1=now_ns2();
    std::printf("planner overhead: %.3f us/plan\n", double(t1-t0)/1000.0/1000.0);
    rt.shutdown();
}

int main() {
    std::printf("Transfer Fabric benchmarks\n");
    bench_host_to_host((std::uint64_t)64u * 1024u * 1024u);
    bench_host_to_host((std::uint64_t)64u * 1024u);
    bench_file((std::uint64_t)64u * 1024u * 1024u);
    bench_cuda((std::uint64_t)64u * 1024u * 1024u);
    bench_planner((std::uint64_t)64u * 1024u * 1024u);
    return 0;
}
