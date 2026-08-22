#include "example_util.hpp"
#include <cuda_runtime.h>
#include "transfer_fabric/platform/cuda_detect.hpp"
int main() {
  if (!cuda_available()) { std::printf("no cuda device; skipping\n"); return 0; }
  const std::size_t n = 1u<<20; std::vector<std::uint8_t> h(n), rb(n,0);
  for (size_t i=0;i<n;++i) h[i]=(std::uint8_t)(i&0xFF);
  void* dev=nullptr; if (cudaMalloc(&dev, n)!=cudaSuccess) return 1;
  Runtime rt({}); EndpointHandle he, de;
  rt.register_endpoint(EndpointDescriptor::host_memory(h.data(), n), he);
  rt.register_endpoint(EndpointDescriptor::device_memory(0, dev, n), de);
  TransferOptions o; o.source=he; o.destination=de; Error e; TransferHandle t = rt.submit(o, e);
  if (!e.ok() || !rt.wait(t)) return 1;
  cudaMemcpy(rb.data(), dev, n, cudaMemcpyDeviceToHost); cudaDeviceSynchronize();
  std::printf("pageable H2D %s\n", std::memcmp(h.data(), rb.data(), n)==0 ? "OK" : "MISMATCH");
  EndpointHandle he2; rt.register_endpoint(EndpointDescriptor::host_memory(rb.data(), n), he2);
  TransferOptions d2h; d2h.source=de; d2h.destination=he2; TransferHandle t2 = rt.submit(d2h, e);
  std::printf("D2H %s\n", (e.ok() && rt.wait(t2) && std::memcmp(h.data(), rb.data(), n)==0) ? "OK" : "MISMATCH");
  rt.shutdown(); cudaFree(dev); return 0;
}
