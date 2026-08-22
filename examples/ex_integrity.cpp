#include "example_util.hpp"
#include "transfer_fabric/integrity.hpp"
int main() {
  std::printf("crc32c(123456789)=%08x sha256(abc)=%s\n", Crc32c::compute("123456789", 9), Sha256::hex(Sha256::compute("abc", 3)).c_str());
  Runtime rt({}); std::size_t sz=1u<<16; std::vector<std::uint8_t> a(sz), b(sz,0);
  for (size_t i=0;i<sz;++i) a[i]=(std::uint8_t)(i&0xFF);
  EndpointHandle se,de; rt.register_endpoint(EndpointDescriptor::host_memory(a.data(), sz), se);
  rt.register_endpoint(EndpointDescriptor::host_memory(b.data(), sz), de);
  TransferOptions o; o.source=se; o.destination=de; o.policy.integrity_mode=VerificationMode::sha256;
  Error e; TransferHandle t=rt.submit(o,e);
  std::printf("transfer with sha256 verification: %s (%llu verified)\n", (e.ok()&&rt.wait(t))?"OK":"FAIL", (unsigned long long)rt.status(t).bytes_verified);
  rt.shutdown(); return 0;
}
