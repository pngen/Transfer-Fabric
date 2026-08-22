#include "transfer_fabric/chunking.hpp"

namespace transfer_fabric {

ChunkPlan ChunkPlan::build(const ChunkPlanInput& in) {
    ChunkPlan p;
    if (in.min_chunk == 0) {
        throw TransferException(Error(ErrorCategory::invalid_request, "min_chunk must be > 0"));
    }
    if (in.max_chunk < in.min_chunk) {
        throw TransferException(Error(ErrorCategory::invalid_request, "max_chunk < min_chunk"));
    }
    if (in.alignment == 0) {
        throw TransferException(Error(ErrorCategory::invalid_request, "alignment must be > 0"));
    }
    p.total_ = in.total;
    p.source_offset_ = in.source_offset;
    p.dest_offset_ = in.dest_offset;
    p.alignment_ = in.alignment;

    // Zero-length transfer: legal no-op with zero chunks.
    if (in.total == 0) {
        p.chunk_size_ = in.preferred_chunk ? in.preferred_chunk : in.min_chunk;
        p.chunk_count_ = 0;
        p.tail_size_ = 0;
        return p;
    }

    byte_count chunk = in.preferred_chunk;
    if (chunk == 0) chunk = in.min_chunk;
    if (chunk < in.min_chunk) chunk = in.min_chunk;
    if (chunk > in.max_chunk) chunk = in.max_chunk;

    // Align chunk size DOWN to the required alignment (never below min_chunk).
    byte_count base = (chunk / in.alignment) * in.alignment;
    if (base == 0) base = in.alignment;
    if (base < in.min_chunk) base = in.min_chunk;
    if (base > in.max_chunk) base = in.max_chunk;

    p.chunk_size_ = base;

    byte_count count = in.total / base;
    byte_count rem = in.total % base;
    if (rem != 0) {
        p.tail_size_ = rem;
        ++count;
    } else {
        p.tail_size_ = 0;
    }
    p.chunk_count_ = count;

    if (count == 0) {
        throw TransferException(Error(ErrorCategory::invalid_request, "internal: zero chunks for non-zero total"));
    }
    return p;
}

Error ChunkPlan::chunk_at(byte_count i, Chunk& out) const noexcept {
    if (i >= chunk_count_) {
        return Error(ErrorCategory::invalid_request, "chunk index out of range");
    }
    out.index = i;
    byte_count off;
    if (!checked_mul(i, chunk_size_, off)) {
        return Error(ErrorCategory::invalid_request, "chunk offset overflow");
    }
    out.source_offset = source_offset_ + off;
    out.dest_offset = dest_offset_ + off;
    if (i + 1 == chunk_count_ && tail_size_ != 0) {
        out.length = tail_size_;
        out.is_tail = true;
    } else {
        out.length = chunk_size_;
        out.is_tail = false;
    }
    byte_count end;
    if (!checked_add(off, out.length, end)) {
        return Error(ErrorCategory::invalid_request, "chunk end overflow");
    }
    if (end > total_) {
        return Error(ErrorCategory::invalid_request, "chunk exceeds total");
    }
    return Error();
}

std::vector<Chunk> ChunkPlan::materialize() const {
    std::vector<Chunk> v;
    v.reserve(static_cast<std::size_t>(chunk_count_));
    for (byte_count i = 0; i < chunk_count_; ++i) {
        Chunk c;
        chunk_at(i, c);
        v.push_back(c);
    }
    return v;
}

std::vector<Chunk> materialize_plan(const ChunkPlan& plan) { return plan.materialize(); }

} // namespace transfer_fabric
