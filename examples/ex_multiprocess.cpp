#include "example_util.hpp"
#include <windows.h>
#include <string>
#include <cstring>

// Multiprocess transfer: parent writes a pattern to a named shared region, then
// launches a child process that reads it and verifies content independently.
int main(int argc, char** argv) {
    const std::string shmname = "TF_MULTIPROCESS_SHM";
    const std::size_t sz = 1u << 18;
    const unsigned char seed = 0x5A;
    if (argc >= 2 && std::string(argv[1]) == "--child") {
        // Child: open the shared region and verify.
        HANDLE hMap = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, shmname.c_str());
        if (!hMap) { std::printf("child: cannot open shm\n"); return 1; }
        auto* p = static_cast<unsigned char*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sz));
        if (!p) { CloseHandle(hMap); return 1; }
        bool ok = true;
        for (std::size_t i=0;i<sz;++i) if (p[i] != (unsigned char)((i*13+seed)&0xFF)) { ok=false; break; }
        std::printf("child: verification %s\n", ok ? "OK" : "FAIL");
        UnmapViewOfFile(p); CloseHandle(hMap);
        return ok ? 0 : 1;
    }
    // Parent: create the shared region and write via Transfer Fabric runtime.
    Runtime rt({});
    HANDLE hMap = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, (DWORD)sz, shmname.c_str());
    if (!hMap) { std::printf("parent: cannot create shm\n"); return 1; }
    auto* p = static_cast<unsigned char*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sz));
    if (!p) { CloseHandle(hMap); return 1; }
    std::vector<std::uint8_t> host(sz);
    for (std::size_t i=0;i<sz;++i) host[i] = (std::uint8_t)((i*13+seed)&0xFF);
    EndpointHandle he, sh;
    rt.register_endpoint(EndpointDescriptor::host_memory(host.data(), sz), he);
    EndpointDescriptor shd = EndpointDescriptor::shared_region(shmname, 0, sz);
    // register the shared region by mapping it locally first (runtime will map the same name)
    rt.register_endpoint(EndpointDescriptor::host_memory(p, sz), sh);
    TransferOptions o; o.source=he; o.destination=sh;
    Error e; TransferHandle t = rt.submit(o, e);
    if (!e.ok() || !rt.wait(t)) { std::printf("parent: transfer failed\n"); return 1; }
    rt.shutdown();
    // Launch child process (same exe).
    char path[MAX_PATH]; GetModuleFileNameA(nullptr, path, MAX_PATH);
    std::string cmd = std::string("\"") + path + "\" --child\"";
    STARTUPINFOA si{}; PROCESS_INFORMATION pi{};
    if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { std::printf("parent: cannot launch child\n"); return 1; }
    WaitForSingleObject(pi.hProcess, 15000);  // child completes quickly; bound for robustness
    DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    std::printf("parent: child exit code %u\n", code);
    UnmapViewOfFile(p); CloseHandle(hMap);
    return code == 0 ? 0 : 1;
}
