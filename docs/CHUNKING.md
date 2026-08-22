# Chunking

A `ChunkPlan` splits a byte range into chunks with overflow-safe arithmetic and exact final-tail handling.

- Configurable min/max/preferred chunk size and alignment.
- Chunk geometry is computed lazily (no per-chunk heap allocation for large transfers).
- Zero-byte transfers are legal no-ops (0 chunks).
- Pathological sizes (0, 1, alignment-1, alignment+1, power-of-two, huge, overflow attempts) are property-tested.
