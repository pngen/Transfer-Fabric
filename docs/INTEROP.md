# Interop

Transfer Fabric is a *movement* runtime and is designed to be embedded, not to own memory or identities.

## Unified Buffer adapter

Transfer Fabric does not duplicate Unified Buffer. To move a Unified Buffer object, a consumer converts its buffer/view into a Transfer Fabric `EndpointDescriptor` (host or device base + size) and registers it. This keeps the core runtime usable without Unified Buffer.

## Topology Fabric

A topology-provider interface answers facts (same host, NUMA node, device association, peer capability, path class, relative distance, link metadata, cost). Transfer Fabric consumes these facts through the interface; it does not model topology itself.

## Bandwidth Governor

Transfer Fabric exposes interfaces for a future governor to approve admission, provide bandwidth limits, allocate tokens, report contention, throttle routes, and revoke allowance. Transfer Fabric executes under those limits and never becomes the global arbiter.
