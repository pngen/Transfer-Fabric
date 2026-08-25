# Transfer Fabric

*The vendor-neutral runtime for governing how bytes move.*

Transfer Fabric 1.0.0 is a C++20 systems runtime for **data movement itself**. It answers a single core question:

> **How should bytes move** from source to destination — through which path, in what chunks, with what staging, overlap, verification, scheduling, flow control, and failure semantics?

It is not a buffer allocator, a cache, a KV store, a compute scheduler, or a topology database. It is the *movement engine* between those systems. It plans, selects, stages, verifies, and coordinates transfers across heterogeneous memory, devices, processes, storage tiers, and execution nodes.

## What it owns

- Transfer planning and path selection
- Direct vs. staged movement
- Chunking, batching, coalescing, splitting
- Pipeline construction, staging, overlap, scheduling
- Flow control, backpressure, bounded queues
- Integrity verification (CRC32C / SHA256)
- Transfer lifecycle, cancellation, transactional completion
- Failure classification, bounded retry
- Multiprocess and distributed transfer coordination
- Telemetry and benchmarkable path behavior

## What it does **not** own

The following systems are explicit non-owners; Transfer Fabric consumes them through interfaces but never absorbs them:

| System | Owns |
|---|---|
| **Unified Buffer** | Buffer allocation policy, buffer identity/pooling, ownership, lifetime |
| **FlashTier** | Residency, promotion/demotion, eviction economics |
| **Tensor Cache** | Tensor admission/reuse/eviction economics |
| **Compute Fabric** | Compute placement |
| **Context Fabric / KV Fabric** | Reusable-state identity and KV/inference semantics |
| **Reclaim Fabric / Checkpoint Fabric** | State-retention and execution-survival policy |

Transfer Fabric may *consume* topology hints and bandwidth limits through interfaces, and is independently usable.

## The core question, concretely

Given a source endpoint and a destination endpoint, the runtime must decide:

- Is a direct path legal? Is a staged path cheaper or required?
- What is the best route (a sequence of legs) under the configured policy and a benchmark-informed cost model?
- How large should each chunk be, and where does the final tail land?
- Which staging pools (host, pinned host) are reserved before any byte moves?
- Do we pipeline the chunks so a chunk can be committed while the next is still being read?
- How do we verify integrity (none / CRC32C / SHA256), and how do we treat a mismatch?
- If a leg fails, is it retryable, and under what bound?
- How do we cancel cleanly without leaking a queue slot or a staging allocation?
- How is completion made transactional enough that a mid-transfer failure never reports success?

## Supported movement paths

- host → host
- pageable host → CUDA device, pinned host → CUDA device
- CUDA device → host (pageable / pinned), CUDA device → CUDA device
- file → host, host → file
- staged file → pinned → CUDA, staged CUDA → pinned → file
- shared memory → host, shared → shared (cross-process)
- remote endpoint → staged local destination (framed TCP)

## Building

Requires a C++20 compiler and CMake (3.24+). CUDA 13.x is used and validated on an NVIDIA RTX 5090 on Windows.

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build --target transfer_fabric transfer-fabric
```

On Windows, build through a toolchain shell that loads MSVC (vcvars) and CUDA; see `scripts/dev-shell.cmd`.

## Layout

```
include/transfer_fabric/   public headers
src/                       core implementation
src/backends/              host, file, shared-memory, CUDA backends
src/transports/            framed TCP transport
src/planner/               route planner + cost model
src/runtime/               transfer sync/async engine, scheduler, registry
src/telemetry/             counters + JSON
src/platform/              CUDA primitives + device detection
tests/                     test suites
examples/                  runnable examples
benchmarks/                benchmark suite
docs/                      documentation
cmake/                     CMake package config
```

## License

Apache License 2.0 — see `LICENSE` and `NOTICE`.
