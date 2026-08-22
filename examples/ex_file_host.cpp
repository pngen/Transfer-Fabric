#include "example_util.hpp"
#include <windows.h>
#include <vector>
static const char* P = "C:\\Temp\\tf_example_file.bin";
int main() {
  const std::size_t sz = 1u<<16; std::vector<std::uint8_t> data(sz);
  for (size_t i=0;i<sz;++i) data[i]=(std::uint8_t)(i*3&0xFF);
  { HANDLE h=CreateFileA(P, GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD w=0; WriteFile(h,data.data(),(DWORD)sz,&w,nullptr); CloseHandle(h); }
  Runtime rt({}); std::vector<std::uint8_t> buf(sz,0);
  EndpointHandle fe, he;
  rt.register_endpoint(EndpointDescriptor::file_region(P, 0, sz), fe);
  rt.register_endpoint(EndpointDescriptor::host_memory(buf.data(), sz), he);
  TransferOptions o; o.source=fe; o.destination=he; Error e; TransferHandle t = rt.submit(o, e);
  std::printf("file->host %s\n", (e.ok() && rt.wait(t) && std::memcmp(data.data(), buf.data(), sz)==0) ? "OK" : "FAIL");
  // host->file
  std::vector<std::uint8_t> src(sz), pre(sz,0);
  for (size_t i=0;i<sz;++i) src[i]=(std::uint8_t)(i&0xFF);
  { HANDLE h=CreateFileA(P, GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD w=0; WriteFile(h,pre.data(),(DWORD)sz,&w,nullptr); CloseHandle(h); }
  EndpointHandle he3, fe3;
  rt.register_endpoint(EndpointDescriptor::host_memory(src.data(), sz), he3);
  rt.register_endpoint(EndpointDescriptor::file_region(P, 0, sz), fe3);
  TransferOptions w; w.source=he3; w.destination=fe3; TransferHandle t3 = rt.submit(w, e);
  std::vector<std::uint8_t> back(sz,0); EndpointHandle he4;
  rt.register_endpoint(EndpointDescriptor::host_memory(back.data(), sz), he4);
  TransferOptions rr; rr.source=fe3; rr.destination=he4; TransferHandle t4 = rt.submit(rr, e);
  std::printf("host->file->host %s\n", (e.ok() && rt.wait(t3) && rt.wait(t4) && std::memcmp(src.data(), back.data(), sz)==0) ? "OK" : "FAIL");
  rt.shutdown(); return 0;
}
