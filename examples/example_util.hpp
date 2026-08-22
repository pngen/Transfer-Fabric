#pragma once
#include "transfer_fabric/runtime.hpp"
#include <cstdio>
#include <vector>
#include <cstring>
using namespace transfer_fabric;
// Run a host<->host transfer of sz bytes and report success.
inline bool run_host_to_host(Runtime& rt, std::size_t sz) {
    std::vector<std::uint8_t> a(sz), b(sz, 0);
    for (std::size_t i=0;i<sz;++i) a[i] = (std::uint8_t)(i & 0xFF);
    EndpointHandle se, de;
    if (!rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se).ok()) return false;
    if (!rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de).ok()) return false;
    TransferOptions o; o.source=se; o.destination=de; o.policy.integrity_mode = VerificationMode::crc32c;
    Error e; TransferHandle h = rt.submit(o, e);
    if (!e.ok()) return false;
    bool done = rt.wait(h);
    return done && std::memcmp(a.data(), b.data(), sz)==0;
}
