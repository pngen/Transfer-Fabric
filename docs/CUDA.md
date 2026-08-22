# CUDA

Transfer Fabric implements real CUDA support and is validated on the installed hardware.

- Device detection queries the CUDA runtime and driver (never hard-codes).
- Supported copies: pageable host → device, pinned host → device (asynchronous/overlap-capable), device → pageable/pinned host, device → device.
- Staged paths: `file -> pinned -> device` and `device -> pinned -> file` route through pinned host staging.
- Asynchronous copies use real CUDA streams; the runtime never frees staging the GPU still references, and shutdown drains outstanding work before releasing resources.
- CUDA errors are surfaced with names/messages.
- Validation measured on an NVIDIA RTX 5090 (compute capability 12.0): pageable H2D, pinned H2D, D2H, and D2D all verified byte-exact, and outstanding allocations return to zero.
- With a single GPU, peer-to-peer copy is an unimplemented capability seam (not claimed as validated).
