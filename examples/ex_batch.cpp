#include "example_util.hpp"
int main() {
  Runtime rt({}); const std::size_t sz = 1u<<15; std::vector<std::uint8_t> a(sz), b(sz,0);
  for (auto& x : a) x = 0x42;
  EndpointHandle se, de; rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
  rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
  BatchHandle batch; rt.begin_batch(batch);
  int ok=0;
  for (int i=0;i<8;++i) {
    TransferOptions o; o.source=se; o.destination=de; o.batch=batch;
    Error e; TransferHandle h = rt.submit(o, e);
    if (e.ok() && rt.wait(h)) ok++;
  }
  rt.end_batch(batch); rt.shutdown();
  std::printf("batch: %d/8 transfers\n", ok);
  return ok==8 ? 0 : 1;
}
