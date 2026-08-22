#include "example_util.hpp"
#include <windows.h>
#include <cuda_runtime.h>
#include "transfer_fabric/platform/cuda_detect.hpp"
int main() {
  const std::size_t sz = 1u<<18; std::string P = "C:\\Temp\\tf_pipe.bin";
  std::vector<std::uint8_t> data(sz); for (size_t i=0;i<sz;++i) data[i]=(std::uint8_t)(i&0xFF);
  { HANDLE h=CreateFileA(P.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD w=0; WriteFile(h,data.data(),(DWORD)sz,&w,nullptr); CloseHandle(h); }
  if (!cuda_available()) { std::printf("no cuda; skipping staged device pipeline\n"); return 0; }
  void* dev=nullptr; cudaMalloc(&dev, sz);
  Runtime rt({}); EndpointHandle fe, de;
  rt.register_endpoint(EndpointDescriptor::file_region(P, 0, sz), fe);
  rt.register_endpoint(EndpointDescriptor::device_memory(0, dev, sz), de);
  TransferOptions o; o.source=fe; o.destination=de; o.policy.preferred_chunk_size = 64u<<10;
  o.policy.allow_staging = true; o.policy.integrity_mode = VerificationMode::crc32c;
  Error e; TransferHandle t = rt.submit(o, e);
  std::vector<std::uint8_t> rb(sz,0);
  if (rt.wait(t)) cudaMemcpy(rb.data(), dev, sz, cudaMemcpyDeviceToHost);
  cudaDeviceSynchronize();
  bool ok = e.ok() && std::memcmp(data.data(), rb.data(), sz)==0;
  std::printf("staged file->pinned->device pipeline: %s (route=%s)\n", ok?"OK":"FAIL", rt.inspect(t).route.description.c_str());
  rt.shutdown(); cudaFree(dev); return ok?0:1;
}
