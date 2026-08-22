# Flow control and backpressure

Execution is bounded:

- max queued transfers (default 256) and max active transfers.
- max staging bytes per pool.
- Per-pool backpressure returns `resource_exhausted` instead of allocation.

The scheduler never silently exceeds a bound. Counters (`queue_depth`, `staging_usage`, high-waters) are exposed through telemetry and JSON.
