#include "transfer_fabric/runtime.hpp"
#include "transfer_fabric/version.hpp"
#include "transfer_fabric/platform/cuda_detect.hpp"
#include "transfer_fabric/telemetry.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <iostream>

using namespace transfer_fabric;

static void print_version() { std::printf("transfer-fabric %s\n", TRANSFER_FABRIC_VERSION); }

static int cmd_info(bool json) {
    if (json) {
        std::printf("{\"version\":\"%s\",\"platform\":\"windows\",\"cuda\":%s}\n",
                    TRANSFER_FABRIC_VERSION, cuda_available() ? "true" : "false");
    } else {
        std::printf("Transfer Fabric %s\n", TRANSFER_FABRIC_VERSION);
        std::printf("platform: windows\n");
        std::printf("cuda: %s\n", cuda_available() ? "available" : "unavailable");
        std::printf("devices: %d\n", platform::cuda::device_count());
    }
    return 0;
}

static int cmd_backends(bool json) {
    Runtime rt({});
    auto b = rt.available_backends();
    if (json) {
        std::printf("{\"backends\":[");
        for (size_t i=0;i<b.size();++i) { if(i) std::printf(","); std::printf("\"%s\"", b[i].c_str()); }
        std::printf("]}\n");
    } else { for (auto& x : b) std::printf("  %s\n", x.c_str()); }
    rt.shutdown();
    return 0;
}

static int cmd_devices(bool json) {
    auto devs = cuda_devices();
    if (json) {
        std::printf("{\"devices\":[");
        for (size_t i=0;i<devs.size();++i) {
            if (i) std::printf(",");
            std::printf("{\"ordinal\":%d,\"name\":\"%s\",\"vram_bytes\":%llu,\"compute\":\"%d.%d\"}",
                        devs[i].ordinal, devs[i].name.c_str(), (unsigned long long)devs[i].vram_bytes,
                        devs[i].compute_major, devs[i].compute_minor);
        }
        std::printf("]}\n");
    } else {
        for (auto& d : devs)
            std::printf("  [%d] %s (%llu bytes, compute %d.%d)\n", d.ordinal, d.name.c_str(),
                        (unsigned long long)d.vram_bytes, d.compute_major, d.compute_minor);
    }
    return 0;
}

static int cmd_domains(bool json) {
    const char* names[] = {"unknown","host_pageable","host_pinned","device","shared","storage","remote","mmap"};
    if (json) { std::printf("{\"domains\":["); for (int i=1;i<8;++i){ if(i>1) std::printf(","); std::printf("\"%s\"", names[i]); } std::printf("]}\n"); }
    else { for (int i=1;i<8;++i) std::printf("  %s\n", names[i]); }
    return 0;
}

static int cmd_stats(bool json) {
    Runtime rt({});
    TelemetrySnapshot s = rt.telemetry();
    if (json) std::printf("%s\n", s.to_json().c_str());
    else std::printf("transfers created=%llu completed=%llu bytes_moved=%llu\n",
                     (unsigned long long)s.transfers_created, (unsigned long long)s.transfers_completed,
                     (unsigned long long)s.bytes_moved);
    rt.shutdown();
    return 0;
}

static int cmd_plan(int argc, char** argv) { (void)argc; (void)argv;
    const std::size_t sz = 1u << 20;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    Runtime rt({});
    EndpointHandle se, de;
    if (!rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se).ok() ||
        !rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de).ok()) return 1;
    PlanResult pr = rt.plan(se, de, sz, TransferPolicy{});
    if (!pr.found) { rt.shutdown(); return 1; }
    std::printf("best route: %s (class=%s hops=%llu cost=%.3f)\n", pr.best.description.c_str(),
                to_string(pr.best.route_class), (unsigned long long)pr.best.hop_count, pr.best.cost.total);
    std::printf("candidates: %zu\n", pr.candidates.size());
    for (auto& r : pr.candidates) std::printf("  - %s cost=%.3f\n", r.description.c_str(), r.cost.total);
    rt.shutdown();
    return 0;
}

static int cmd_transfer(int argc, char** argv) { (void)argc; (void)argv;
    const std::size_t sz = 1u << 20;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (size_t i=0;i<sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    Runtime rt({});
    EndpointHandle se, de;
    if (!rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se).ok() ||
        !rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de).ok()) return 1;
    TransferOptions opts; opts.source=se; opts.destination=de;
    opts.policy.integrity_mode = VerificationMode::crc32c;
    Error err; TransferHandle h = rt.submit(opts, err);
    if (!err.ok()) { std::printf("transfer failed: %s\n", err.message.c_str()); return 1; }
    bool done = rt.wait(h);
    auto st = rt.status(h);
    std::printf("transfer complete=%d state=%s bytes=%llu verified=%llu\n", done, to_string(st.state),
                (unsigned long long)st.bytes_completed, (unsigned long long)st.bytes_verified);
    rt.shutdown();
    return done ? 0 : 1;
}

static int cmd_selftest() {
    Runtime rt({});
    const std::size_t sz = 1u << 16;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (size_t i=0;i<sz;++i) a[i]=(std::uint8_t)(i*7&0xFF);
    EndpointHandle se, de;
    if (!rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se).ok() ||
        !rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de).ok()) return 1;
    TransferOptions opts; opts.source=se; opts.destination=de;
    opts.policy.integrity_mode = VerificationMode::sha256;
    Error err; TransferHandle h = rt.submit(opts, err);
    bool ok = err.ok() && rt.wait(h) && (std::memcmp(a.data(), b.data(), sz)==0);
    rt.shutdown();
    std::printf("selftest: %s\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}

static int cmd_benchmark() {
    Runtime rt({});
    const std::size_t sz = 64u << 20;
    std::vector<std::uint8_t> a(sz), b(sz,0);
    for (size_t i=0;i<sz;++i) a[i]=(std::uint8_t)(i&0xFF);
    EndpointHandle se, de;
    rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
    rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
    TransferOptions opts; opts.source=se; opts.destination=de;
    Error err; TransferHandle h = rt.submit(opts, err);
    if (!err.ok()) return 1;
    bool done = rt.wait(h);
    auto s = rt.telemetry();
    double secs = (double)s.execution_time_ns / 1e9;
    double gbps = secs>0 ? ((double)sz/1e9)/secs : 0;
    std::printf("host->host %zu bytes in %.5f s = %.3f GiB/s\n", sz, secs, gbps);
    rt.shutdown();
    return done ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) { print_version(); return 0; }
    bool json = false;
    std::string cmd = argv[1];
    for (int i=2;i<argc;++i) if (std::string(argv[i])=="--json") json=true;
    if (cmd=="info") return cmd_info(json);
    if (cmd=="backends") return cmd_backends(json);
    if (cmd=="devices") return cmd_devices(json);
    if (cmd=="domains") return cmd_domains(json);
    if (cmd=="stats") return cmd_stats(json);
    if (cmd=="plan") return cmd_plan(argc, argv);
    if (cmd=="transfer") return cmd_transfer(argc, argv);
    if (cmd=="selftest") return cmd_selftest();
    if (cmd=="benchmark") return cmd_benchmark();
    if (cmd=="batch") { std::printf("batch: not implemented in CLI\n"); return 0; }
    if (cmd=="inspect" || cmd=="cancel") { std::printf("inspect/cancel: query through Runtime API\n"); return 0; }
    if (cmd=="paths") { std::printf("paths: see ROUTING.md\n"); return 0; }
    print_version();
    return 0;
}
