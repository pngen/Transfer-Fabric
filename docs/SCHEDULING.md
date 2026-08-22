# Scheduling

The scheduler is bounded and priority-aware: a multi-level FIFO queue over a fixed worker pool, a bounded queue depth, and deterministic shutdown.

- No busy spin, no unbounded queues, no hidden thread explosion.
- Workers are created once; `shutdown` drains pending tasks then joins all workers.
- The default `submit` path is synchronous; the async path submits a transfer driver task.
