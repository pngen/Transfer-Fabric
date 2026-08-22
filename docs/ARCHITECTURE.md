# Architecture

Transfer Fabric is layered so that planning, execution, and transport remain separable.

## Layers

1. **Public API (`Runtime`)** — the facade. Registers endpoints, plans transfers, submits and waits, cancels, inspects, and exposes telemetry.
2. **Planner** — computes a set of candidate routes over a domain graph, scores them with a benchmark-informed cost model, and returns the best route + all candidates.
3. **Scheduler** — a bounded priority scheduler over a fixed worker pool. The default submit path is synchronous (correct, deadlock-free); the scheduler provides the async path.
4. **Executor** — walks a selected route chunk-by-chunk, using a bounded staging ring to pipeline chunks and CUDA streams to overlap host reads with device copies.
5. **Backends** — host (memcpy), file/storage (Win32 I/O), shared-memory, and CUDA. Extensibility seams for HIP/Level Zero/Vulkan/RDMA.
6. **Transports** — framed TCP used for distributed/remote transfer.
7. **Staging pools** — bounded byte pools for host and pinned-host staging.
8. **Telemetry** — thread-safe counters and a JSON snapshot.

## Ordering of a transfer

```
plan -> reserve -> stage -> execute -> verify -> commit -> retire
```

Staging is reserved before any byte moves; a failure before commit releases reservations and staging, preserves the source, and never claims destination authority.

## Thread model

- The scheduler owns a fixed pool of worker threads. No busy spin, no hidden thread creation.
- The default `submit` executes a transfer synchronously on the calling thread, which guarantees ordering and makes cancellation-cleanup deterministic.
- The async path (`queue_transfer`) submits a driver task to the scheduler.

## Deadlock-avoidance invariants

- Completion is signaled with a predicate-conditioned `condition_variable`, so no notification can be missed.
- Locks are never held while waiting for a worker to join, and `shutdown` joins all workers before draining pools.
- No path acquires a read lock and then upgrades it to a write lock on the same mutex.
- No user callback runs while an internal lock is held.
