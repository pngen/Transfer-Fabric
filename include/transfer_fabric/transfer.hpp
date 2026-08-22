#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/handle.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/policy.hpp"
#include "transfer_fabric/integrity.hpp"

namespace transfer_fabric {

// Priority band. Higher values are scheduled first.
enum class TF_API Priority : std::uint8_t {
    background = 0,
    low        = 1,
    normal     = 4,
    high       = 7,
    urgent     = 9,
};

// QoS class carried for metadata and routing. Not a bandwidth reservation.
enum class TF_API QosClass : std::uint8_t {
    best_effort = 0,
    throughput,
    latency,
};

TF_API inline const char* to_string(Priority p) noexcept {
    switch (p) {
        case Priority::background: return "background";
        case Priority::low:        return "low";
        case Priority::normal:     return "normal";
        case Priority::high:       return "high";
        case Priority::urgent:     return "urgent";
    }
    return "normal";
}

// Every piece of information the runtime needs to plan and execute one transfer.
// The runtime never mutates this after admission; it is copied into the live
// transfer record.
struct TF_API TransferOptions {
    EndpointHandle source{};      // source endpoint (validated at submit)
    EndpointHandle destination{}; // destination endpoint (writable)
    ByteRange source_range{};     // default = whole source region
    ByteRange destination_range{};// default = whole destination region

    TransferPolicy policy;
    Priority       priority{Priority::normal};
    QosClass       qos{QosClass::best_effort};

    // Identity / authority.
    std::string    name_space;    // namespace for policy/accounting isolation
    std::string    principal;     // owner/principal string, optional

    // Optional explicit id (for deterministic tests). Zero => generate.
    TransferHandle explicit_id{};

    // Batching / dependencies.
    BatchHandle    batch{};
    std::vector<TransferId> dependencies{};

    // Deadline metadata (informational; surfaced to backends if a backend models one).
    bool           has_deadline{false};
    std::uint64_t  deadline_ns{0};

    // Verification override over the policy (NONE causes policy to be ignored).
    bool           verification_override{false};
    VerificationMode verification_mode{VerificationMode::none};
};

// Completion reason for waiters.
enum class TF_API CompletionReason : std::uint8_t {
    completed,
    cancelled,
    failed,
    rolled_back,
};

} // namespace transfer_fabric
