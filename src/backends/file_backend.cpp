#include "transfer_fabric/backends/file.hpp"

#include <cstring>
#include <vector>

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace transfer_fabric {

CapabilitySet FileBackend::capabilities() const {
    CapabilitySet c;
    c.set(Capability::readable);
    c.set(Capability::writable);
    c.set(Capability::persistent);
    c.set(Capability::direct_copy);
    return c;
}

bool FileBackend::can_copy(const CopySpec& spec) const {
    bool src_storage = spec.src_kind == EndpointKind::file || spec.src_kind == EndpointKind::mmap;
    bool dst_storage = spec.dst_kind == EndpointKind::file || spec.dst_kind == EndpointKind::mmap;
    if (!src_storage && !dst_storage) return false;
    // Neither side may be a device; storage moves only through the host backend
    // at the planner level or directly file<->host / file<->file here.
    bool src_device = spec.src_kind == EndpointKind::device;
    bool dst_device = spec.dst_kind == EndpointKind::device;
    if (src_device || dst_device) return false;
    return true;
}

namespace {
bool read_at(std::uint64_t handle, byte_offset off, void* buf, byte_count n, byte_count* got) {
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle));
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(off);
    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
    byte_count total = 0;
    while (total < n) {
        DWORD chunk = static_cast<DWORD>(n - total > 1u << 30 ? (1u << 30) : (n - total));
        DWORD read = 0;
        if (!ReadFile(h, static_cast<char*>(buf) + total, chunk, &read, nullptr)) {
            return false;
        }
        if (read == 0) break; // EOF
        total += read;
    }
    if (got) *got = total;
    return total == n;
#else
    (void)handle; (void)off; (void)buf; (void)n; (void)got;
    return false;
#endif
}

bool write_at(std::uint64_t handle, byte_offset off, const void* buf, byte_count n, byte_count* wrote) {
#if defined(_WIN32)
    HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(handle));
    LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(off);
    SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
    byte_count total = 0;
    while (total < n) {
        DWORD chunk = static_cast<DWORD>(n - total > 1u << 30 ? (1u << 30) : (n - total));
        DWORD wr = 0;
        if (!WriteFile(h, static_cast<const char*>(buf) + total, chunk, &wr, nullptr)) {
            return false;
        }
        if (wr == 0) break;
        total += wr;
    }
    if (total == n) FlushFileBuffers(h);   // make the data durable/visible
    if (wrote) *wrote = total;
    return total == n;
#else
    (void)handle; (void)off; (void)buf; (void)n; (void)wrote;
    return false;
#endif
}
} // namespace

Error FileBackend::copy(const CopySpec& spec) {
    bool src_storage = spec.src_kind == EndpointKind::file || spec.src_kind == EndpointKind::mmap;
    bool dst_storage = spec.dst_kind == EndpointKind::file || spec.dst_kind == EndpointKind::mmap;
    if (spec.bytes == 0) return Error();

    if (src_storage && dst_storage) {
        // file -> file: stream through a bounded heap buffer.
        std::vector<char> tmp(1u << 20);
        byte_count done = 0;
        while (done < spec.bytes) {
            byte_count togo = spec.bytes - done;
            byte_count thisn = togo > tmp.size() ? tmp.size() : togo;
            byte_count got = 0, wr = 0;
            if (!read_at(spec.src_object, spec.src_obj_offset + done, tmp.data(), thisn, &got))
                return Error(ErrorCategory::backend_failure, "file read failed");
            if (got != thisn)
                return Error(ErrorCategory::backend_failure, "short read during file->file copy");
            if (!write_at(spec.dst_object, spec.dst_obj_offset + done, tmp.data(), thisn, &wr))
                return Error(ErrorCategory::backend_failure, "file write failed");
            if (wr != thisn)
                return Error(ErrorCategory::backend_failure, "short write during file->file copy");
            done += thisn;
        }
        return Error();
    }
    if (src_storage) {
        // file -> host
        byte_count got = 0;
        if (!read_at(spec.src_object, spec.src_obj_offset, spec.dst_ptr, spec.bytes, &got))
            return Error(ErrorCategory::backend_failure, "file read failed");
        if (got != spec.bytes)
            return Error(ErrorCategory::backend_failure, "short read from storage source");
        return Error();
    }
    // host -> file
    byte_count wr = 0;
    if (!write_at(spec.dst_object, spec.dst_obj_offset, spec.src_ptr, spec.bytes, &wr))
        return Error(ErrorCategory::backend_failure, "file write failed");
    if (wr != spec.bytes)
        return Error(ErrorCategory::backend_failure, "short write to storage destination");
    return Error();
}

} // namespace transfer_fabric
