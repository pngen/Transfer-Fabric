# Benchmarks

The benchmark suite measures real throughput and validates content independently of timing:

- host → host (large and small)
- file → host
- pageable H2D (and D2D)
- planner overhead

Results on the validation host: host→host ~21.6 GiB/s, file→host ~5.3 GiB/s, pageable H2D ~3.1 GiB/s, planner ~11 µs/plan. Backend bandwidths in the cost model are calibrated to these classes.
