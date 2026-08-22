#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/route.hpp"
#include "transfer_fabric/policy.hpp"
#include "transfer_fabric/capabilities.hpp"

namespace transfer_fabric {

// Everything the planner needs to reason about a single transfer.
struct TF_API PlannerInput {
    EndpointCapabilities src;
    EndpointCapabilities dst;
    byte_count bytes{0};
    TransferPolicy policy;

    // Staging resource availability (bytes). The planner refuses routes that
    // need more staging than is available rather than silently overallocating.
    byte_count host_staging_capacity{0};
    byte_count pinned_staging_capacity{0};

    // Whether a remote transport is present (for remote transfers).
    bool remote_available{false};

    // Backends the runtime actually has. Empty == assume all known backends.
    std::vector<std::string> available_backends;
};

// Result of planning: the chosen route plus all feasible candidates (for
// inspection/reporting). If no route is found, found==false and reason explains.
struct TF_API PlanResult {
    bool found{false};
    Route best;
    std::vector<Route> candidates;
    std::string reason;
};

// Route planning strategy. Independent of execution.
class TF_API RoutePlanner {
public:
    virtual ~RoutePlanner() = default;
    virtual PlanResult plan(const PlannerInput& in) const = 0;
};

// Cost model: turns a route + byte count into a numeric cost under the policy
// weights. The model is deterministic and benchmark-calibratable, and it is
// honest that it is an estimate, not ground truth.
class TF_API CostModel {
public:
    static CostEstimate evaluate(const Route& route, byte_count bytes,
                                 const RouteWeights& w) noexcept;
    static CostEstimate evaluate(const std::vector<Leg>& legs, byte_count bytes,
                                 const RouteWeights& w) noexcept;
};

// Default concrete planner implementing the domain-graph route search.
class TF_API DefaultRoutePlanner : public RoutePlanner {
public:
    PlanResult plan(const PlannerInput& in) const override;

    // Expose the deterministic route id derivation for inspection/tests.
    static std::string route_id(const std::vector<Leg>& legs);
    static std::string describe(const std::vector<Leg>& legs);
};

} // namespace transfer_fabric
