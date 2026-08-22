#include "example_util.hpp"
#include "transfer_fabric/planner.hpp"
int main() {
  const std::size_t sz = 1u<<20; std::vector<std::uint8_t> a(sz), b(sz,0);
  Runtime rt({}); EndpointHandle se, de;
  rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
  rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
  TransferPolicy pol; PlanResult pr = rt.plan(se, de, sz, pol);
  std::printf("best=%s class=%s hops=%llu cost=%.3f candidates=%zu\n", pr.best.description.c_str(),
              to_string(pr.best.route_class), (unsigned long long)pr.best.hop_count, pr.best.cost.total, pr.candidates.size());
  for (auto& r : pr.candidates) std::printf("  candidate %s cost=%.3f\n", r.description.c_str(), r.cost.total);
  rt.shutdown(); return 0;
}
