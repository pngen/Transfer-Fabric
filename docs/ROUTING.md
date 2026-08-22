# Routing

## Domain graph

Routes are found by a bounded depth-first search over a small domain graph (nodes = memory domains, edges = backend copy operations). Source and destination domains are the start and goal.

## Direct vs staged

- A **direct** route is a single leg with no staging.
- A **staged** route inserts one or more staging buffers (e.g. `file -> host_pinned -> device`).
- The planner prefers direct routes when the policy says so and a direct route exists; otherwise it ranks staged routes by cost.

## Route identity and cost

Each route has a deterministic id and a cost:

```
cost = fixed_setup + bytes/bandwidth + staging_penalty + hop_penalty + queue_penalty
       + verification_penalty + retry_risk
```

Bandwidths are calibrated defaults; a benchmark may feed measured values back into the model.
