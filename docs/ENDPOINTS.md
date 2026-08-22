# Endpoints

Endpoints describe *externally owned* resources through safe descriptors. Transfer Fabric never allocates the underlying memory.

Endpoint kinds: `host`, `device`, `file`, `shared`, `mmap`, `remote`.

Each endpoint carries a capability descriptor: readable, writable, CPU/accelerator addressable, DMA, direct/staged/async/peer/overlap/multiprocess/persistence/verification, required alignment, maximum and preferred transfer size, supported chunk sizes, backend/device identity, and locality.

Endpoints are registered with the `Runtime` and receive generation-aware `EndpointHandle`s; stale, forged, or wrong-generation handles are rejected. Registering a `file` endpoint rejects path-parent components (`..`) by default. File endpoints are opened `OPEN_EXISTING`, so sources and destinations must exist before registration (creation is a higher-level concern). The file/shm handles are closed on endpoint destruction.
