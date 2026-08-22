#pragma once

#include <cstdint>
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/integrity.hpp"

namespace transfer_fabric {

using policy_version_t = std::uint32_t;

// Weights used by the route cost model. Higher = more significant/expensive.
struct TF_API RouteWeights {
    double fixed_setup{1.0};
    double bandwidth{1.0};
    double staging{1.0};
    double hop{1.0};
    double queue{1.0};
    double verification{0.5};
    double retry_risk{1.0};
    double concurrency{0.2};
};

// How cancellation is handled once work has begun.
enum class TF_API CancellationBehavior : std::uint8_t {
    cooperative,  // let an in-flight leg finish, then stop (preferred)
    forced,       // attempt to interrupt in-flight work
};

TF_API inline const char* to_string(CancellationBehavior b) noexcept {
    return b == CancellationBehavior::cooperative ? "cooperative" : "forced";
}

// A versioned, opaque movement policy. Policies are immutable; use with_copies to
// produce a modified policy with its own version. A policy carries zero pointer
// state and is safe to share across threads.
class TF_API TransferPolicy {
public:
    TransferPolicy();

    policy_version_t version() const noexcept { return version_; }
    TransferPolicy with_version(policy_version_t v) const { TransferPolicy c = *this; c.version_ = v; return c; }

    // --- direct vs staged ---
    bool prefer_direct{true};
    bool allow_staging{true};
    bool require_staging{false};

    // --- structure limits ---
    byte_count max_hops{8};
    byte_count max_staging_bytes{1u << 30};

    // --- chunking ---
    byte_count preferred_chunk_size{64u * 1024u};
    byte_count max_chunk_size{256u * 1024u * 1024u};

    // --- integrity ---
    VerificationMode integrity_mode{VerificationMode::none};
    bool per_chunk_integrity{false};

    // --- retry ---
    std::uint32_t retry_limit{3};

    // --- scheduling ---
    std::uint32_t queue_priority{4};      // 0..8, higher = more important
    std::uint32_t concurrency_limit{0};   // 0 == runtime default / unbounded

    // --- allowlists ---
    std::vector<std::string> backend_allowlist;   // empty == allow all known
    std::set<MemoryDomain>   domain_allowlist;    // empty == allow all

    // --- authority ---
    bool remote_transfer_permission{false};
    bool persistence_permission{true};

    // --- cancellation ---
    CancellationBehavior cancellation_behavior{CancellationBehavior::cooperative};

    // --- cost model ---
    RouteWeights weights;

    // --- inspection ---
    bool domain_allowed(MemoryDomain d) const noexcept {
        return domain_allowlist.empty() || domain_allowlist.count(d) != 0;
    }
    bool backend_allowed(const std::string& name) const noexcept {
        return backend_allowlist.empty() || std::find(backend_allowlist.begin(), backend_allowlist.end(), name) != backend_allowlist.end();
    }

private:
    policy_version_t version_{0};
};

} // namespace transfer_fabric
