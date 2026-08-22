#include "example_util.hpp"
int main() {
  Runtime rt({}); const std::size_t sz = 1u<<16;
  std::vector<std::uint8_t> src(sz), back(sz, 0);
  for (auto& x : src) x = 0x33;
  EndpointHandle sh, he, he2;
  rt.register_endpoint(EndpointDescriptor::shared_region("TF_EX_SHM", 0, sz), sh);
  rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), sz), he);
  rt.register_endpoint(EndpointDescriptor::host_memory(back.data(), sz), he2);
  TransferOptions o; o.source=he; o.destination=sh; Error e;
  TransferHandle hw = rt.submit(o, e);
  bool wok = e.ok() && rt.wait(hw);
  TransferOptions r; r.source=sh; r.destination=he2; Error e2;
  TransferHandle th = rt.submit(r, e2);
  bool rok = e2.ok() && rt.wait(th);
  std::printf("shared memory host->shared->host: %s\n", (wok && rok && std::memcmp(src.data(), back.data(), sz)==0) ? "OK" : "FAIL");
  rt.shutdown(); return (wok&&rok)?0:1;
}
