#include "example_util.hpp"
#include "transfer_fabric/transports/tcp.hpp"
#include <windows.h>
#include <thread>
int main() {
  std::vector<std::uint8_t> data(1u<<14); for (size_t i=0;i<data.size();++i) data[i]=(std::uint8_t)(i*7&0xFF);
  std::string src = "C:\\Temp\\tf_remote_src.bin", dst = "C:\\Temp\\tf_remote_dst.bin";
  { HANDLE h=CreateFileA(src.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD w=0; WriteFile(h,data.data(),(DWORD)data.size(),&w,nullptr); CloseHandle(h); }
  TcpChunkServer server; if (!server.listen(TcpTransport::default_port()).ok()) return 1;
  std::thread t([&]{ server.serve_once(); });
  TcpChunkClient client; if (!client.connect("127.0.0.1", TcpTransport::default_port()).ok()) return 1;
  Error e = client.transfer(src, 0, data.size(), dst, TransferId::generate());
  client.close(); if (t.joinable()) t.join(); server.close();
  std::vector<std::uint8_t> got(data.size(), 0);
  { HANDLE h=CreateFileA(dst.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr); DWORD r=0; ReadFile(h,got.data(),(DWORD)got.size(),&r,nullptr); CloseHandle(h); }
  std::printf("TCP framed transfer: %s\n", (e.ok() && std::memcmp(data.data(), got.data(), data.size())==0) ? "OK" : "FAIL");
  return e.ok()?0:1;
}
