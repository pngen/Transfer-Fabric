#pragma once

#include <cstdint>
#include <array>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/errors.hpp"
#include "transfer_fabric/types.hpp"

namespace transfer_fabric {

// Transfer lifecycle. State transitions are strictly enforced by the runtime;
// any transition not in the table below is a logic error and is rejected.
enum class TF_API TransferState : std::uint8_t {
    created,       // just constructed
    planned,       // route selected, chunk plan built
    reserved,      // staging resources reserved
    queued,        // admitted to a scheduler queue
    active,        // at least one leg/chunk is executing
    verifying,     // integrity verification in progress
    committing,    // commit/retirement phase
    completed,     // terminal success
    cancelled,     // terminal cancellation
    failed,        // terminal failure
    rolled_back,   // terminal partial-work rollback
};

TF_API inline const char* to_string(TransferState s) noexcept {
    switch (s) {
        case TransferState::created:    return "created";
        case TransferState::planned:    return "planned";
        case TransferState::reserved:   return "reserved";
        case TransferState::queued:     return "queued";
        case TransferState::active:     return "active";
        case TransferState::verifying:  return "verifying";
        case TransferState::committing: return "committing";
        case TransferState::completed:  return "completed";
        case TransferState::cancelled:  return "cancelled";
        case TransferState::failed:     return "failed";
        case TransferState::rolled_back:return "rolled_back";
    }
    return "unknown";
}

TF_API inline bool is_terminal(TransferState s) noexcept {
    return s == TransferState::completed || s == TransferState::cancelled
        || s == TransferState::failed || s == TransferState::rolled_back;
}

TF_API inline bool is_running(TransferState s) noexcept {
    return s == TransferState::queued || s == TransferState::active
        || s == TransferState::verifying || s == TransferState::committing;
}

// Exhaustive transition table. Two adjacent booleans index [from][to].
struct TF_API StateTransitionTable {
    static constexpr int N = 11; // sizeof(TransferState) value set
    using Row = std::array<bool, N>;
    static const std::array<Row, N>& table();
    static bool allowed(TransferState from, TransferState to) noexcept {
        return table()[static_cast<int>(from)][static_cast<int>(to)];
    }
};

// A snapshot of transfer progress/status, safe to read from any thread.
struct TF_API TransferStatus {
    TransferState state{TransferState::created};
    Error error;
    byte_count bytes_scheduled{0};
    byte_count bytes_submitted{0};
    byte_count bytes_completed{0};
    byte_count bytes_verified{0};
    byte_count chunk_count{0};
    byte_count chunks_done{0};
    std::uint64_t attempt{0};
    std::uint64_t retries{0};
    std::uint64_t created_ns{0};
    std::uint64_t queued_ns{0};
    std::uint64_t started_ns{0};
    std::uint64_t done_ns{0};
};

} // namespace transfer_fabric
