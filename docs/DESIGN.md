# Design

## Principles

- **Deterministic where expected**: same inputs + same runtime state -> same plan.
- **Explicit authority**: policies gate what is allowed; unsupported combinations fail, not silently fall back.
- **Bounded**: all queues, staging pools, and active work are bounded.
- **Observable**: every counter is exposed; telemetry is a JSON snapshot.
- **Failure-aware & rollback-capable**: partial work is retired before commit; no false success.
- **Integrity-aware**: CRC32C / SHA256 verification, hard failure on mismatch.
- **Concurrency-safe**: all shared state is behind locks or atomics; a lock-order audit is applied.
- **Resource-accounted**: counters return to zero at shutdown.

## Asynchronous execution

The runtime's default `submit` is asynchronous: it plans, reserves staging, and queues a transfer driver task to the bounded scheduler, then returns a handle immediately. The caller observes completion with `wait`/status. This gives real cross-transfer concurrency across the worker pool, correct in-flight cancellation, and predictable drain-on-shutdown. Intra-transfer pipelining is real: a staged `file -> pinned -> device` route reads a chunk into the staging ring while the previous chunk is copied asynchronously on a CUDA stream. The scheduler uses a predicate-based condition-variable wait (accurate work count `queue_depth_`, `notify_all`) so no notification can be missed; `shutdown` drains pending work then joins all workers before pools are released.
