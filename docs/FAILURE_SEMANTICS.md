# Failure semantics

Failures are classified: invalid request, unsupported path, resource exhausted, quota exceeded, path unavailable, backend failure, transient transport, integrity failure, cancellation, stale generation, permission denied, timeout (external), permanent failure.

- Retry is bounded; no infinite retries.
- `transient_transport` / `path_unavailable` / `resource_exhausted` are retryable categories.
- A failure before commit releases reservations and staging, preserves the source, and never claims destination authority.
- Cancellation is first-class; a transfer that completes is never reported as cancelled, and vice versa.
