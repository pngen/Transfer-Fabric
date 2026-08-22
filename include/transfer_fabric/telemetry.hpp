#pragma once

#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/types.hpp"
#include "transfer_fabric/endpoint.hpp"
#include "transfer_fabric/route.hpp"

namespace transfer_fabric {

// A plain, copyable snapshot of runtime telemetry. Safe to render to JSON.
struct TF_API TelemetrySnapshot {
    // lifecycle counters
    std::uint64_t transfers_created{0};
    std::uint64_t transfers_planned{0};
    std::uint64_t transfers_queued{0};
    std::uint64_t transfers_active{0};
    std::uint64_t transfers_completed{0};
    std::uint64_t transfers_failed{0};
    std::uint64_t transfers_cancelled{0};
    std::uint64_t transfers_retried{0};

    // bytes
    std::uint64_t bytes_requested{0};
    std::uint64_t bytes_moved{0};
    std::uint64_t bytes_verified{0};
    std::uint64_t bytes_staged{0};
    std::uint64_t direct_path_bytes{0};
    std::uint64_t staged_path_bytes{0};

    // structural counters
    std::uint64_t chunk_count{0};
    std::uint64_t batch_count{0};

    // gauges / high-water
    std::uint64_t queue_depth{0};
    std::uint64_t queue_high_water{0};
    std::uint64_t active_byte_high_water{0};
    std::uint64_t staging_usage{0};
    std::uint64_t staging_high_water{0};
    double        staging_pool_hit_rate{0.0};

    // planner
    std::uint64_t planner_route_selections{0};
    std::uint64_t planner_fallbacks{0};

    // failures / outcomes
    std::uint64_t integrity_failures{0};
    std::uint64_t backend_failures{0};
    std::uint64_t transport_failures{0};
    std::uint64_t retry_count{0};
    std::uint64_t cancellation_count{0};

    // timing & performance
    std::uint64_t planner_time_ns{0};
    std::uint64_t queue_wait_time_ns{0};
    std::uint64_t execution_time_ns{0};
    std::uint64_t verification_time_ns{0};
    double        throughput_bytes_per_sec{0.0};

    // per-key buckets
    std::array<std::uint64_t, 16> per_domain_bytes{};
    std::array<std::uint64_t, 16> per_device_bytes{};
    std::array<std::uint64_t, 8>  per_route_class_bytes{};
    std::unordered_map<std::string, std::uint64_t> per_backend_bytes;
    std::unordered_map<std::string, std::uint64_t> per_route_bytes;

    std::string to_json() const;
};

// Thread-safe telemetry accumulator. Hot counters are atomics; bucket maps are
// lock-protected. Reads take a cheap snapshot.
class TF_API Telemetry {
public:
    Telemetry();

    // lifecycle
    void transfer_created() noexcept { created_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_planned() noexcept { planned_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_queued() noexcept { queued_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_started() noexcept { active_.fetch_add(1, std::memory_order_relaxed); record_active_high_water(); }
    void transfer_done() noexcept { active_.fetch_sub(1, std::memory_order_relaxed); }
    void transfer_completed() noexcept { completed_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_failed() noexcept { failed_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_cancelled() noexcept { cancelled_.fetch_add(1, std::memory_order_relaxed); }
    void transfer_retried() noexcept { retried_.fetch_add(1, std::memory_order_relaxed); }

    // bytes
    void add_bytes_requested(byte_count n) noexcept { bytes_requested_.fetch_add(n, std::memory_order_relaxed); }
    void add_bytes_moved(byte_count n) noexcept { bytes_moved_.fetch_add(n, std::memory_order_relaxed); }
    void add_bytes_verified(byte_count n) noexcept { bytes_verified_.fetch_add(n, std::memory_order_relaxed); }
    void add_bytes_staged(byte_count n) noexcept { bytes_staged_.fetch_add(n, std::memory_order_relaxed); }
    void add_direct_path_bytes(byte_count n) noexcept { direct_.fetch_add(n, std::memory_order_relaxed); }
    void add_staged_path_bytes(byte_count n) noexcept { staged_.fetch_add(n, std::memory_order_relaxed); }

    void add_chunks(std::uint64_t n) noexcept { chunks_.fetch_add(n, std::memory_order_relaxed); }
    void add_batch() noexcept { batches_.fetch_add(1, std::memory_order_relaxed); }

    void queue_pushed() noexcept { queue_depth_.fetch_add(1, std::memory_order_relaxed); record_depth_high_water(); }
    void queue_popped() noexcept { queue_depth_.fetch_sub(1, std::memory_order_relaxed); }

    void staging_allocated(byte_count n) noexcept { staging_.fetch_add(n, std::memory_order_relaxed); record_staging_high_water(); }
    void staging_released(byte_count n) noexcept { staging_.fetch_sub(n, std::memory_order_relaxed); }
    void staging_pool_hit(bool hit) noexcept;

    void planner_selected() noexcept { selections_.fetch_add(1, std::memory_order_relaxed); }
    void planner_fallback() noexcept { fallbacks_.fetch_add(1, std::memory_order_relaxed); }
    void integrity_failure() noexcept { integrity_.fetch_add(1, std::memory_order_relaxed); }
    void backend_failure() noexcept { backend_.fetch_add(1, std::memory_order_relaxed); }
    void transport_failure() noexcept { transport_.fetch_add(1, std::memory_order_relaxed); }
    void retry() noexcept { retry_count_.fetch_add(1, std::memory_order_relaxed); }
    void cancellation() noexcept { cancellation_count_.fetch_add(1, std::memory_order_relaxed); }

    // per-key buckets
    void add_domain_bytes(MemoryDomain d, byte_count n) noexcept;
    void add_device_bytes(device_id_t d, byte_count n) noexcept;
    void add_route_class_bytes(RouteClass c, byte_count n) noexcept;
    void add_backend_bytes(const std::string& name, byte_count n) noexcept;
    void add_route_bytes(const std::string& route_id, byte_count n) noexcept;

    // timing
    void add_planner_time(std::uint64_t ns) noexcept { planner_ns_.fetch_add(ns, std::memory_order_relaxed); }
    void add_queue_wait_time(std::uint64_t ns) noexcept { queue_ns_.fetch_add(ns, std::memory_order_relaxed); }
    void add_execution_time(std::uint64_t ns) noexcept { exec_ns_.fetch_add(ns, std::memory_order_relaxed); }
    void add_verification_time(std::uint64_t ns) noexcept { verify_ns_.fetch_add(ns, std::memory_order_relaxed); }

    TelemetrySnapshot snapshot() const;

private:
    void record_active_high_water() noexcept;
    void record_depth_high_water() noexcept;
    void record_staging_high_water() noexcept;

    std::atomic<std::uint64_t> created_{0}, planned_{0}, queued_{0}, active_{0}, completed_{0};
    std::atomic<std::uint64_t> failed_{0}, cancelled_{0}, retried_{0};
    std::atomic<std::uint64_t> bytes_requested_{0}, bytes_moved_{0}, bytes_verified_{0}, bytes_staged_{0};
    std::atomic<std::uint64_t> direct_{0}, staged_{0}, chunks_{0}, batches_{0};
    std::atomic<std::uint64_t> queue_depth_{0}, queue_hw_{0}, active_hw_{0};
    std::atomic<std::uint64_t> staging_{0}, staging_hw_{0};
    std::atomic<std::uint64_t> selections_{0}, fallbacks_{0}, integrity_{0}, backend_{0}, transport_{0};
    std::atomic<std::uint64_t> retry_count_{0}, cancellation_count_{0};
    std::atomic<std::uint64_t> planner_ns_{0}, queue_ns_{0}, exec_ns_{0}, verify_ns_{0};
    std::atomic<std::uint64_t> staging_hits_{0}, staging_misses_{0};

    mutable std::mutex bucket_mutex_;
    std::array<std::uint64_t, 16> per_domain_{};
    std::array<std::uint64_t, 16> per_device_{};
    std::array<std::uint64_t, 8>  per_route_class_{};
    std::unordered_map<std::string, std::uint64_t> per_backend_;
    std::unordered_map<std::string, std::uint64_t> per_route_;
};

} // namespace transfer_fabric
