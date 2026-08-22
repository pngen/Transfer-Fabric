# Testing

The test suite is split into standalone executables:

- `test_smoke` — smoke, lifecycle, state machine, transfer id.
- `test_core` — CRC32C/SHA256 vectors, chunking pathologies, planner routes, host→host verification, corruption, stale handles, cancellation-after-completion, staging pool bounds, accounting closure.
- `test_storage` — framed protocol, malformed/partial frames, file→host, host→file, path-traversal rejection, short read, shared memory, TCP server/client transfer.
- `test_cuda` — real CUDA device detection, pageable/pinned H2D, D2D, cleanup-to-zero.

Every randomized/property run leaves no active transfers, no leaked staging, and accounting returns to zero.
