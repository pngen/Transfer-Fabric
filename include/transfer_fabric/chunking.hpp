#pragma once

#include <cstdint>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/errors.hpp"

namespace transfer_fabric {

// A single transfer chunk with explicit source/destination offsets.
struct TF_API Chunk {
    byte_count  index{0};
    byte_offset source_offset{0};
    byte_offset dest_offset{0};
    byte_count  length{0};
    bool        is_tail{false};

    friend constexpr bool operator==(const Chunk&, const Chunk&) = default;
};

// Parameters for chunk planning.
struct TF_API ChunkPlanInput {
    byte_count total{0};
    byte_offset source_offset{0};
    byte_offset dest_offset{0};
    byte_count  min_chunk{1};
    byte_count  max_chunk{byte_count(~0ULL)};
    byte_count  preferred_chunk{64u * 1024u};
    byte_count  alignment{1};
};

// A memory-efficient chunk plan. Chunks are uniform except for the final tail,
// so per-chunk geometry is computed on demand rather than materialised.
class TF_API ChunkPlan {
public:
    ChunkPlan() = default;

    // Builds a plan using the given input. Throws TransferException on invalid
    // input (zero/oversized chunk sizes, alignment overflow, etc.).
    static ChunkPlan build(const ChunkPlanInput& in);

    byte_count total() const noexcept { return total_; }
    byte_count chunk_size() const noexcept { return chunk_size_; }
    byte_count chunk_count() const noexcept { return chunk_count_; }
    byte_count alignment() const noexcept { return alignment_; }
    byte_count source_offset() const noexcept { return source_offset_; }
    byte_count dest_offset() const noexcept { return dest_offset_; }

    // Size of chunk i (in [0, chunk_count_)).
    Error chunk_at(byte_count i, Chunk& out) const noexcept;
    // Exact tail chunk size (0 if no tail).
    byte_count tail_chunk_size() const noexcept { return tail_size_; }

    // Serialise all chunks into a vector (useful for debug/inspection).
    std::vector<Chunk> materialize() const;

    bool valid() const noexcept { return chunk_count_ == 0 || chunk_size_ > 0; }

private:
    byte_count total_{0};
    byte_count chunk_size_{0};
    byte_count chunk_count_{0};
    byte_count alignment_{1};
    byte_count source_offset_{0};
    byte_count dest_offset_{0};
    byte_count tail_size_{0};
};

// Helpers for tests and property checks.
TF_API std::vector<Chunk> materialize_plan(const ChunkPlan& plan);

} // namespace transfer_fabric
