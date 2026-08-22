#include "internal.hpp"

#include <cstring>
#include <memory>
#include <vector>

#include "transfer_fabric/platform/cuda.hpp"
#include "transfer_fabric/integrity.hpp"

#if TF_ENABLE_CUDA
#  include <cuda_runtime.h>
#endif
#if defined(_WIN32)
#  include <windows.h>
#endif

namespace transfer_fabric {

namespace {
struct Loc {
    void*         ptr{nullptr};
    std::uint64_t obj{0};
    byte_offset   off{0};
    MemoryDomain  domain{MemoryDomain::unknown};
    device_id_t   device{0};
    EndpointKind  kind{EndpointKind::unknown};
};

struct Ring {
    std::vector<StagingBuffer> bufs;
    byte_count size{0};
    MemoryDomain domain{MemoryDomain::host_pageable};
};

bool is_storage(EndpointKind k) noexcept {
    // mmap regions are CPU-addressable via a mapped pointer (host backend), so
    // only true file/object storage uses the storage object+offset path.
    return k == EndpointKind::file;
}
bool is_memory_addr(MemoryDomain d) noexcept {
    return d == MemoryDomain::host_pageable || d == MemoryDomain::host_pinned
        || d == MemoryDomain::shared || d == MemoryDomain::mmap;
}

std::size_t progress_update;
} // namespace

// Hash a region of an endpoint in the requested integrity mode.
Error hash_endpoint_region(std::shared_ptr<EndpointRecord> ep, byte_offset offset,
                           byte_count len, VerificationMode mode,
                           std::array<std::uint8_t, 32>& digest_out) {
    digest_out.fill(0);
    if (mode == VerificationMode::none || len == 0) return Error();
    const uint8_t* p = nullptr;

    if (ep->desc.kind == EndpointKind::file) {
        // storage: read through the file handle
        using C = Crc32c; using S = Sha256;
        C crc; S sha; bool use_crc = mode == VerificationMode::crc32c;
        const byte_count tmp_sz = 1u << 20;
        std::vector<char> buf(static_cast<std::size_t>(tmp_sz));
        HANDLE h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(ep->file_handle));
        byte_count done = 0;
        while (done < len) {
            byte_count n = (len - done) > tmp_sz ? tmp_sz : (len - done);
            LARGE_INTEGER li; li.QuadPart = static_cast<LONGLONG>(ep->desc.offset + offset + done);
            SetFilePointerEx(h, li, nullptr, FILE_BEGIN);
            DWORD rd = 0;
            if (!ReadFile(h, buf.data(), static_cast<DWORD>(n), &rd, nullptr) || static_cast<byte_count>(rd) != n) {
                return Error(ErrorCategory::integrity_failure, "hash: short read");
            }
            if (use_crc) crc.update(buf.data(), n); else sha.update(buf.data(), n);
            done += n;
        }
        if (use_crc) { std::uint32_t v = crc.value(); for (int i=0;i<4;++i) digest_out[i] = std::uint8_t((v >> (8*i)) & 0xFF); }
        else { auto d = sha.digest(); digest_out = d; }
        return Error();
    }

    if (ep->desc.kind == EndpointKind::device) {
        // read back to a pageable temp buffer and hash
        const byte_count tmp_sz = 16u << 20;
        std::vector<std::uint8_t> buf(static_cast<std::size_t>(tmp_sz));
        Crc32c crc; Sha256 sha; bool use_crc = mode == VerificationMode::crc32c;
        byte_count done = 0;
        while (done < len) {
            byte_count n = (len - done) > tmp_sz ? tmp_sz : (len - done);
            char* devptr = static_cast<char*>(ep->desc.base) + (offset + done);
            cudaMemcpy(buf.data(), devptr, static_cast<std::size_t>(n), cudaMemcpyDeviceToHost);
            if (use_crc) crc.update(buf.data(), n); else sha.update(buf.data(), n);
            done += n;
        }
        if (use_crc) { std::uint32_t v = crc.value(); for (int i=0;i<4;++i) digest_out[i] = std::uint8_t((v >> (8*i)) & 0xFF); }
        else { auto d = sha.digest(); digest_out = d; }
        return Error();
    }

    // CPU-addressable host / shared / mmap region
    p = static_cast<const uint8_t*>(ep->desc.base) + (offset);
    if (mode == VerificationMode::crc32c) {
        std::uint32_t v = Crc32c::compute(p, len);
        for (int i = 0; i < 4; ++i) digest_out[i] = std::uint8_t((v >> (8*i)) & 0xFF);
    } else {
        auto d = Sha256::compute(p, len);
        digest_out = d;
    }
    return Error();
}

namespace {
void build_spec(const Loc& src, const Loc& dst, byte_count bytes, byte_count align, CopySpec& out) {
    out.src_ptr = src.ptr;
    out.dst_ptr = dst.ptr;
    out.bytes = bytes;
    out.alignment = align;
    out.src_domain = src.domain;
    out.dst_domain = dst.domain;
    out.src_device = src.device;
    out.dst_device = dst.device;
    out.src_kind = src.kind;
    out.dst_kind = dst.kind;
    out.src_object = src.obj;
    out.dst_object = dst.obj;
    out.src_obj_offset = src.off;
    out.dst_obj_offset = dst.off;
}
} // namespace

Error run_transfer(RunContext& ctx) {
    auto& rec = ctx.record;
    auto& route = rec->route;
    auto& plan = rec->chunk_plan;
    const std::size_t nlegs = route.legs.size();
    (void)progress_update;

    if (route.empty() || plan.chunk_count() == 0) {
        // zero-byte / empty request completes immediately.
        std::lock_guard<std::mutex> g(rec->mtx);
        rec->status.bytes_completed = plan.total();
        return Error();
    }

    // Build node chain: nodes[0]=src, nodes[n]=dst.
    std::vector<MemoryDomain> nodes;
    nodes.reserve(nlegs + 1);
    nodes.push_back(route.legs[0].source_domain);
    for (const auto& L : route.legs) nodes.push_back(L.dest_domain);
    const std::size_t nnodes = nodes.size(); // == nlegs+1

    // Intermediate staging nodes are 1..nnodes-2. Buffers were reserved at
    // queue time (state RESERVED) and are stored on the record.
    const std::size_t depth = ctx.pipeline_depth > 0 ? ctx.pipeline_depth : 2;
    std::vector<Ring> rings(nnodes);
    for (std::size_t j = 1; j + 1 < nnodes; ++j) {
        Ring& r = rings[j];
        r.domain = nodes[j];
        r.size = plan.chunk_size();
        if (j < rec->staging_bufs.size()) {
            for (auto& b : rec->staging_bufs[j]) r.bufs.push_back(b);
        }
        if (r.bufs.empty()) {
            return Error(ErrorCategory::resource_exhausted, "staging buffers not reserved");
        }
    }

    // Find the final leg's backend.
    const std::string& final_backend_name = route.legs.back().backend;
    auto fbit = ctx.backends->find(final_backend_name);
    if (fbit == ctx.backends->end()) return Error(ErrorCategory::unsupported_path, "unknown backend");
    Backend* final_backend = fbit->second.get();
    auto* stream_backend = dynamic_cast<IStreamBackend*>(final_backend);

    // Decide whether to use an async, overlap-capable pipeline.
    bool use_async = false;
    if (stream_backend != nullptr && nlegs >= 2) {
        const Leg& last = route.legs.back();
        bool last_is_memory = is_memory_addr(last.source_domain) || is_memory_addr(last.dest_domain);
        use_async = last_is_memory;
    }
    std::vector<void*> streams;
    if (use_async) {
        for (std::size_t k = 0; k < depth; ++k) {
            void* s = nullptr;
            if (!stream_backend->create_stream(s).ok()) { use_async = false; break; }
            streams.push_back(s);
        }
        if (!use_async) for (auto* s : streams) stream_backend->destroy_stream(s);
    }

    // Build a location for node j and chunk c.
    auto loc_at = [&](std::size_t j, const Chunk& c) -> Loc {
        Loc out;
        if (j == 0) {
            auto& ep = rec->src_ep;
            out.domain = ep->caps.memory_domain;
            out.device = ep->caps.device_id;
            out.kind = ep->desc.kind;
            if (is_storage(ep->desc.kind)) {
                out.obj = ep->file_handle;
                out.off = c.source_offset;
            } else {
                out.ptr = static_cast<char*>(ep->desc.base) + c.source_offset;
            }
        } else if (j + 1 == nnodes) {
            auto& ep = rec->dst_ep;
            out.domain = ep->caps.memory_domain;
            out.device = ep->caps.device_id;
            out.kind = ep->desc.kind;
            if (is_storage(ep->desc.kind)) {
                out.obj = ep->file_handle;
                out.off = c.dest_offset;
            } else {
                out.ptr = static_cast<char*>(ep->desc.base) + c.dest_offset;
            }
        } else {
            const Ring& r = rings[j];
            std::size_t s = static_cast<std::size_t>(c.index % depth);
            out.ptr = r.bufs[s].data;
            out.domain = r.domain;
            out.device = 0;
            out.kind = EndpointKind::host;
        }
        return out;
    };

    auto run_leg = [&](std::size_t li, const Chunk& c) -> Error {
        const Leg& L = route.legs[li];
        auto it = ctx.backends->find(L.backend);
        if (it == ctx.backends->end()) return Error(ErrorCategory::unsupported_path, "unknown backend");
        Backend* b = it->second.get();
        Loc src = loc_at(li, c);
        Loc dst = loc_at(li + 1, c);
        CopySpec spec;
        build_spec(src, dst, c.length, L.alignment, spec);
        if (!b->can_copy(spec)) {
            return Error(ErrorCategory::unsupported_path, "backend cannot copy leg");
        }
        return b->copy(spec);
    };

    const std::size_t count = static_cast<std::size_t>(plan.chunk_count());
    bool failed = false;
    Error result;
    byte_count completed = 0;

    for (std::size_t ci = 0; ci < count; ++ci) {
        if (rec->cancel.load(std::memory_order_relaxed)) {
            result = Error(ErrorCategory::cancellation, "cancelled during transfer");
            failed = true;
            break;
        }
        Chunk c;
        if (!plan.chunk_at(static_cast<byte_count>(ci), c).ok()) {
            result = Error(ErrorCategory::invalid_request, "chunk_at failed");
            failed = true;
            break;
        }
        std::size_t si = use_async ? (ci % depth) : 0;
        if (use_async && ci >= depth) {
            if (!stream_backend->sync(streams[si]).ok()) { /* surfaced at final sync below */ }
        }
        // run legs 0..nlegs-2 synchronously
        bool leg_ok = true;
        for (std::size_t li = 0; li + 1 < nlegs; ++li) {
            Error e = run_leg(li, c);
            if (!e.ok()) { result = e; leg_ok = false; break; }
        }
        if (!leg_ok) { failed = true; break; }
        // final leg
        if (use_async) {
            const Leg& L = route.legs.back();
            auto it = ctx.backends->find(L.backend);
            Loc src = loc_at(nlegs - 1, c);
            Loc dst = loc_at(nlegs, c);
            CopySpec spec;
            build_spec(src, dst, c.length, L.alignment, spec);
            Error e = it->second->can_copy(spec) ? stream_backend->async_copy(spec, streams[si]) : Error(ErrorCategory::unsupported_path, "cannot async copy");
            if (!e.ok()) { result = e; failed = true; break; }
        } else {
            Error e = run_leg(nlegs - 1, c);
            if (!e.ok()) { result = e; failed = true; break; }
        }
        completed += c.length;
        // progress
        {
            std::lock_guard<std::mutex> g(rec->mtx);
            rec->status.bytes_completed = completed;
            rec->status.chunks_done = ci + 1;
        }
    }

    // Drain outstanding async copies.
    if (use_async && !failed) {
        for (auto* s : streams) {
            Error e = stream_backend->sync(s);
            if (!e.ok()) { result = e; failed = true; break; }
        }
    }

    // Release staging buffers (into the pool free list).
    for (std::size_t j = 0; j < nnodes; ++j) {
        for (auto& b : rings[j].bufs) {
            StagingPool* pool = (rings[j].domain == MemoryDomain::host_pinned) ? ctx.pinned_pool : ctx.host_pool;
            pool->release(b);
            if (ctx.telemetry) ctx.telemetry->staging_pool_hit(false);
        }
    }
    // Destroy streams.
    if (use_async) for (auto* s : streams) stream_backend->destroy_stream(s);

    if (failed) return result;

    // ---- Verification -------------------------------------------------
    {
        std::lock_guard<std::mutex> g(rec->mtx);
        rec->status.state = TransferState::verifying;
    }
    VerificationMode mode = rec->opts.policy.integrity_mode;
    if (rec->opts.verification_override) mode = rec->opts.verification_mode;
    if (mode != VerificationMode::none && plan.total() > 0) {
        std::array<std::uint8_t, 32> sd{};
        std::array<std::uint8_t, 32> dd{};
        byte_count src_off = plan.source_offset();
        byte_count dst_off = plan.dest_offset();
        Error es = hash_endpoint_region(rec->src_ep, src_off, plan.total(), mode, sd);
        Error ed = hash_endpoint_region(rec->dst_ep, dst_off, plan.total(), mode, dd);
        if (!es.ok()) {
            std::lock_guard<std::mutex> g(rec->mtx); rec->status.error = es; return es;
        }
        if (!ed.ok()) {
            std::lock_guard<std::mutex> g(rec->mtx); rec->status.error = ed; return ed;
        }
        if (sd != dd) {
            Error eintegr(ErrorCategory::integrity_failure, "destination content differs from source");
            std::lock_guard<std::mutex> g(rec->mtx); rec->status.bytes_verified = 0;
            return eintegr;
        }
        {
            std::lock_guard<std::mutex> g(rec->mtx);
            rec->status.bytes_completed = plan.total();
            rec->status.bytes_verified = plan.total();
        }
        return Error();
    }

    {
        std::lock_guard<std::mutex> g(rec->mtx);
        rec->status.bytes_completed = plan.total();
        rec->status.bytes_verified = 0;
    }
    return Error();
}

} // namespace transfer_fabric
