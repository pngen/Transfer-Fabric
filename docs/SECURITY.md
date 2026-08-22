# Security and adversarial robustness

The runtime treats all input as untrusted:

- Transfer IDs are 128-bit and never reused; forged/unknown ids are rejected.
- Endpoint handles carry a generation; stale/forged/wrong-generation handles are rejected.
- Ranges, offsets, and lengths use checked arithmetic; out-of-bounds and overflow requests are rejected before any copy.
- The framed transport validates magic, version, and every payload length against a bound before allocation; malformed frames are rejected; partial frames are handled; peer-provided lengths are never trusted.
- Path-parent components (`..`) are rejected for file endpoints.
- Namespace/cross-namespace authority, domain allowlists, and backend allowlists gate admission.
- Zero-length edge cases, huge frame claims, truncated frames, and checksum mismatches are covered by tests.
