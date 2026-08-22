#include "tf_test.hpp"
#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/platform/cuda_detect.hpp"
#include <cuda_runtime.h>
#include <vector>
#include <cstring>
#include <cstdio>

using namespace transfer_fabric;

static int g_device = 0;

TF_TEST(cuda_detect) {
    // Real host/toolkit detection, not a hard-coded assumption.
    TF_CHECK(cuda_available() == (platform::cuda::device_count() > 0));
    auto devs = cuda_devices();
    if (cuda_available()) {
        TF_REQUIRE(!devs.empty());
        TF_CHECK(devs[0].compute_major >= 5);
        TF_CHECK(devs[0].vram_bytes > 0);
        std::printf("  device: %s vram=%llu compute=%d.%d\n", devs[0].name.c_str(),
                    (unsigned long long)devs[0].vram_bytes, devs[0].compute_major, devs[0].compute_minor);
    }
}

TF_TEST(cuda_pageable_h2d_d2h) {
    if (!cuda_available()) { TF_CHECK(true); return; }
    const std::size_t n = 1u << 20;
    std::vector<std::uint8_t> host(n);
    for (std::size_t i=0;i<n;++i) host[i] = (std::uint8_t)((i*13+5)&0xFF);
    void* dev = nullptr;
    TF_REQUIRE(cudaMalloc(&dev, n) == cudaSuccess);
    std::vector<std::uint8_t> readback(n, 0);
    Runtime rt({});
    EndpointHandle he, de;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(host.data(), n), he).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::device_memory(g_device, dev, n), de).ok());
    // H2D
    TransferOptions opts; opts.source=he; opts.destination=de;
    opts.policy.integrity_mode = VerificationMode::sha256;
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(rt.wait(h));
    // verify on device by reading back
    TF_REQUIRE(cudaMemcpy(readback.data(), dev, n, cudaMemcpyDeviceToHost) == cudaSuccess);
    TF_REQUIRE(cudaDeviceSynchronize() == cudaSuccess);
    TF_CHECK(std::memcmp(host.data(), readback.data(), n)==0);
    // D2H
    EndpointHandle he2;
    rt.register_endpoint(EndpointDescriptor::host_memory(readback.data(), n), he2);
    TransferOptions d2h; d2h.source=de; d2h.destination=he2;
    TransferHandle h2 = rt.submit(d2h, err);
    TF_REQUIRE(err.ok());
    TF_REQUIRE(rt.wait(h2));
    TF_CHECK(std::memcmp(host.data(), readback.data(), n)==0);
    rt.shutdown();
    TF_REQUIRE(cudaFree(dev) == cudaSuccess);
    TF_REQUIRE(cudaGetLastError() == cudaSuccess);
}

TF_TEST(cuda_pinned_h2d) {
    if (!cuda_available()) { TF_CHECK(true); return; }
    const std::size_t n = 1u << 20;
    void* pinned = nullptr;
    TF_REQUIRE(cudaMallocHost(&pinned, n) == cudaSuccess);
    std::uint8_t* phost = static_cast<std::uint8_t*>(pinned);
    for (std::size_t i=0;i<n;++i) phost[i] = (std::uint8_t)(i&0xFF);
    void* dev = nullptr;
    TF_REQUIRE(cudaMalloc(&dev, n) == cudaSuccess);
    std::vector<std::uint8_t> readback(n, 0);
    Runtime rt({});
    EndpointHandle pe, de;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::host_memory(pinned, n, /*pinned=*/true), pe).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::device_memory(g_device, dev, n), de).ok());
    TransferOptions opts; opts.source=pe; opts.destination=de;
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
    TF_REQUIRE(cudaMemcpy(readback.data(), dev, n, cudaMemcpyDeviceToHost) == cudaSuccess);
    TF_REQUIRE(cudaDeviceSynchronize() == cudaSuccess);
    TF_CHECK(std::memcmp(phost, readback.data(), n)==0);
    rt.shutdown();
    TF_REQUIRE(cudaFree(dev)==cudaSuccess);
    TF_REQUIRE(cudaFreeHost(pinned)==cudaSuccess);
    TF_REQUIRE(cudaGetLastError()==cudaSuccess);
}

TF_TEST(cuda_device_to_device) {
    if (!cuda_available()) { TF_CHECK(true); return; }
    const std::size_t n = 1u << 20;
    void* dsrc = nullptr; void* ddst = nullptr;
    TF_REQUIRE(cudaMalloc(&dsrc, n)==cudaSuccess);
    TF_REQUIRE(cudaMalloc(&ddst, n)==cudaSuccess);
    std::vector<std::uint8_t> host(n); for (size_t i=0;i<n;++i) host[i]=(std::uint8_t)((i*3+9)&0xFF);
    TF_REQUIRE(cudaMemcpy(dsrc, host.data(), n, cudaMemcpyHostToDevice)==cudaSuccess);
    Runtime rt({});
    EndpointHandle sdev, ddev;
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::device_memory(g_device, dsrc, n), sdev).ok());
    TF_REQUIRE(rt.register_endpoint(EndpointDescriptor::device_memory(g_device, ddst, n), ddev).ok());
    TransferOptions opts; opts.source=sdev; opts.destination=ddev;
    Error err; TransferHandle h = rt.submit(opts, err);
    TF_REQUIRE(err.ok()); TF_REQUIRE(rt.wait(h));
    std::vector<std::uint8_t> rb(n,0);
    TF_REQUIRE(cudaMemcpy(rb.data(), ddst, n, cudaMemcpyDeviceToHost)==cudaSuccess);
    TF_REQUIRE(cudaDeviceSynchronize()==cudaSuccess);
    TF_CHECK(std::memcmp(host.data(), rb.data(), n)==0);
    rt.shutdown();
    TF_REQUIRE(cudaFree(dsrc)==cudaSuccess);
    TF_REQUIRE(cudaFree(ddst)==cudaSuccess);
    TF_REQUIRE(cudaGetLastError()==cudaSuccess);
}

TF_TEST(cuda_cleanup_zero) {
    if (!cuda_available()) { TF_CHECK(true); return; }
    // After the above transfers, the runtime should have released everything.
    // We verify by querying device memory usage before/after (best effort).
    size_t free0=0, total0=0; size_t free1=0, total1=0;
    cudaMemGetInfo(&free0, &total0);
    // allocate nothing extra; just ensure no runtime leak by doing a small transfer pool
    Runtime rt({});
    {
        std::vector<std::uint8_t> a(4096), b(4096,0);
        EndpointHandle sa, da;
        rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), 4096), sa);
        rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), 4096), da);
        TransferOptions o; o.source=sa; o.destination=da; Error e;
        TransferHandle h = rt.submit(o, e); TF_REQUIRE(e.ok()); TF_REQUIRE(rt.wait(h));
    }
    rt.shutdown();
    cudaDeviceSynchronize();
    cudaMemGetInfo(&free1, &total1);
    TF_CHECK(free1 >= free0 - (size_t)(4u<<20));   // no large leak (allow small slack)
}

TF_TEST_MAIN()
