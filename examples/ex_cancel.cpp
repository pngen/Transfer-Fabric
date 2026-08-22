#include "example_util.hpp"
int main() {
  Runtime rt({}); const std::size_t sz = 1u<<16; std::vector<std::uint8_t> a(sz), b(sz,0);
  EndpointHandle se, de; rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
  rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
  TransferOptions o; o.source=se; o.destination=de; Error e; TransferHandle h = rt.submit(o, e);
  bool done = e.ok() && rt.wait(h);
  // Cancellation after completion is rejected (already terminal), which is the correct,
  // no-corruption semantic.
  Error ce = rt.cancel(h);
  std::printf("cancel-after-completion rejected=%d state=%s\n", !ce.ok(), to_string(rt.status(h).state));
  rt.shutdown(); return done ? 0 : 1;
}
