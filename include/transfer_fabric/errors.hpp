#pragma once

#include <cstdint>
#include <ostream>
#include <string>
#include <stdexcept>

#include "transfer_fabric/export.hpp"

namespace transfer_fabric {

// Coarse failure classification. A transfer ends in exactly one of these when it
// does not complete. Categories drive retry policy and whether the runtime may
// safely re-issue a leg.
enum class TF_API ErrorCategory : std::uint8_t {
    none             = 0,   // no error / success
    invalid_request,        // malformed input, bad handle, bad range
    unsupported_path,       // no legal route exists
    resource_exhausted,     // bounded pool/queue exhausted
    quota_exceeded,         // configured quota exceeded
    path_unavailable,       // path/device disappeared or is down
    backend_failure,        // backend reported a failure (e.g. cuda error)
    transient_transport,    // temporary transport/network failure (retryable)
    integrity_failure,      // verification/corruption detected
    cancellation,           // cancelled by authority
    stale_generation,       // holder mutated after generation invalidation
    permission_denied,      // not permitted by policy/authority
    timeout_external,       // external deadline surfaced by the backend
    permanent_failure,      // retrying cannot succeed
};

TF_API inline const char* to_string(ErrorCategory c) noexcept {
    switch (c) {
        case ErrorCategory::none:                return "none";
        case ErrorCategory::invalid_request:     return "invalid_request";
        case ErrorCategory::unsupported_path:    return "unsupported_path";
        case ErrorCategory::resource_exhausted:  return "resource_exhausted";
        case ErrorCategory::quota_exceeded:      return "quota_exceeded";
        case ErrorCategory::path_unavailable:    return "path_unavailable";
        case ErrorCategory::backend_failure:     return "backend_failure";
        case ErrorCategory::transient_transport: return "transient_transport";
        case ErrorCategory::integrity_failure:   return "integrity_failure";
        case ErrorCategory::cancellation:        return "cancellation";
        case ErrorCategory::stale_generation:    return "stale_generation";
        case ErrorCategory::permission_denied:   return "permission_denied";
        case ErrorCategory::timeout_external:    return "timeout_external";
        case ErrorCategory::permanent_failure:   return "permanent_failure";
    }
    return "unknown";
}

// Whether the runtime may safely retry a leg given this failure category.
TF_API inline bool retryable(ErrorCategory c) noexcept {
    switch (c) {
        case ErrorCategory::transient_transport:
        case ErrorCategory::path_unavailable:
        case ErrorCategory::backend_failure:
        case ErrorCategory::resource_exhausted:
            return true;
        default:
            return false;
    }
}

// A structured error carrying a category, a backend/driver code, and a human
// readable message. Deliberately lightweight.
struct TF_API Error {
    ErrorCategory category{ErrorCategory::none};
    std::uint64_t backend_code{0};
    std::string   message;

    Error() = default;
    Error(ErrorCategory c, std::string m) : category(c), message(std::move(m)) {}
    Error(ErrorCategory c, std::uint64_t code, std::string m)
        : category(c), backend_code(code), message(std::move(m)) {}

    explicit operator bool() const noexcept { return category != ErrorCategory::none; }
    bool ok() const noexcept { return category == ErrorCategory::none; }

    friend std::ostream& operator<<(std::ostream& os, const Error& e) {
        os << to_string(e.category);
        if (e.backend_code) os << " (code=" << e.backend_code << ")";
        if (!e.message.empty()) os << ": " << e.message;
        return os;
    }
};

// Exception form for APIs that prefer exceptions. Carries the same Error.
class TF_API TransferException : public std::runtime_error {
public:
    explicit TransferException(Error e)
        : std::runtime_error(e.message.empty()
              ? std::string(to_string(e.category))
              : (std::string(to_string(e.category)) + ": " + e.message)),
          error_(std::move(e)) {}
    const Error& error() const noexcept { return error_; }
    ErrorCategory category() const noexcept { return error_.category; }
private:
    Error error_;
};

} // namespace transfer_fabric
