#include "transfer_fabric/telemetry.hpp"

#include <sstream>
#include <cstdio>

namespace transfer_fabric {

Telemetry::Telemetry() = default;

void Telemetry::record_active_high_water() noexcept {
    std::uint64_t a = active_.load(std::memory_order_acquire);
    std::uint64_t hw = active_hw_.load(std::memory_order_relaxed);
    while (hw < a) { if (active_hw_.compare_exchange_weak(hw, a)) break; }
}
void Telemetry::record_depth_high_water() noexcept {
    std::uint64_t d = queue_depth_.load(std::memory_order_acquire);
    std::uint64_t hw = queue_hw_.load(std::memory_order_relaxed);
    while (hw < d) { if (queue_hw_.compare_exchange_weak(hw, d)) break; }
}
void Telemetry::record_staging_high_water() noexcept {
    std::uint64_t s = staging_.load(std::memory_order_acquire);
    std::uint64_t hw = staging_hw_.load(std::memory_order_relaxed);
    while (hw < s) { if (staging_hw_.compare_exchange_weak(hw, s)) break; }
}

void Telemetry::staging_pool_hit(bool hit) noexcept {
    if (hit) staging_hits_.fetch_add(1, std::memory_order_relaxed);
    else staging_misses_.fetch_add(1, std::memory_order_relaxed);
}

void Telemetry::add_domain_bytes(MemoryDomain d, byte_count n) noexcept {
    auto idx = static_cast<std::size_t>(d);
    if (idx >= per_domain_.size()) return;
    std::lock_guard<std::mutex> g(bucket_mutex_); per_domain_[idx] += n;
}
void Telemetry::add_device_bytes(device_id_t d, byte_count n) noexcept {
    auto idx = static_cast<std::size_t>(d);
    if (idx >= per_device_.size()) return;
    std::lock_guard<std::mutex> g(bucket_mutex_); per_device_[idx] += n;
}
void Telemetry::add_route_class_bytes(RouteClass c, byte_count n) noexcept {
    auto idx = static_cast<std::size_t>(c);
    if (idx >= per_route_class_.size()) return;
    std::lock_guard<std::mutex> g(bucket_mutex_); per_route_class_[idx] += n;
}
void Telemetry::add_backend_bytes(const std::string& name, byte_count n) noexcept {
    std::lock_guard<std::mutex> g(bucket_mutex_); per_backend_[name] += n;
}
void Telemetry::add_route_bytes(const std::string& route_id, byte_count n) noexcept {
    std::lock_guard<std::mutex> g(bucket_mutex_); per_route_[route_id] += n;
}

namespace {
void append_json_string(std::string& out, const char* s) {
    out += '"';
    for (; *s; ++s) {
        char c = *s;
        switch (c) {
            case '"' : out += '\\'; out += '"'; break;
            case '\\': out += '\\'; out += '\\'; break;
            case '\n': out += '\\'; out += 'n'; break;
            case '\r': out += '\\'; out += 'r'; break;
            case '\t': out += '\\'; out += 't'; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else out += c;
        }
    }
    out += '"';
}
void append_key(std::ostringstream& os, const std::string& key, bool& first) {
    if (!first) os << ',';
    first = false;
    os << '"' << key << '"' << ':';
}
} // namespace

TelemetrySnapshot Telemetry::snapshot() const {
    TelemetrySnapshot s;
    s.transfers_created = created_.load();
    s.transfers_planned = planned_.load();
    s.transfers_queued = queued_.load();
    s.transfers_active = active_.load();
    s.transfers_completed = completed_.load();
    s.transfers_failed = failed_.load();
    s.transfers_cancelled = cancelled_.load();
    s.transfers_retried = retried_.load();
    s.bytes_requested = bytes_requested_.load();
    s.bytes_moved = bytes_moved_.load();
    s.bytes_verified = bytes_verified_.load();
    s.bytes_staged = bytes_staged_.load();
    s.direct_path_bytes = direct_.load();
    s.staged_path_bytes = staged_.load();
    s.chunk_count = chunks_.load();
    s.batch_count = batches_.load();
    s.queue_depth = queue_depth_.load();
    s.queue_high_water = queue_hw_.load();
    s.active_byte_high_water = active_hw_.load();
    s.staging_usage = staging_.load();
    s.staging_high_water = staging_hw_.load();
    std::uint64_t hits = staging_hits_.load();
    std::uint64_t misses = staging_misses_.load();
    std::uint64_t total = hits + misses;
    s.staging_pool_hit_rate = total ? static_cast<double>(hits) / static_cast<double>(total) : 0.0;
    s.planner_route_selections = selections_.load();
    s.planner_fallbacks = fallbacks_.load();
    s.integrity_failures = integrity_.load();
    s.backend_failures = backend_.load();
    s.transport_failures = transport_.load();
    s.retry_count = retry_count_.load();
    s.cancellation_count = cancellation_count_.load();
    s.planner_time_ns = planner_ns_.load();
    s.queue_wait_time_ns = queue_ns_.load();
    s.execution_time_ns = exec_ns_.load();
    s.verification_time_ns = verify_ns_.load();
    {
        std::lock_guard<std::mutex> g(bucket_mutex_);
        s.per_domain_bytes = per_domain_;
        s.per_device_bytes = per_device_;
        s.per_route_class_bytes = per_route_class_;
        s.per_backend_bytes = per_backend_;
        s.per_route_bytes = per_route_;
    }
    std::uint64_t moved = s.bytes_moved;
    std::uint64_t exec = s.execution_time_ns;
    s.throughput_bytes_per_sec = (exec != 0 && moved != 0)
        ? (static_cast<double>(moved) * 1e9) / static_cast<double>(exec) : 0.0;
    return s;
}

std::string TelemetrySnapshot::to_json() const {
    std::ostringstream os;
    bool first;
    os << '{';
    first = true;
#define TF_KV(name) append_key(os, #name, first); os << name
    TF_KV(transfers_created); TF_KV(transfers_planned); TF_KV(transfers_queued);
    TF_KV(transfers_active); TF_KV(transfers_completed); TF_KV(transfers_failed);
    TF_KV(transfers_cancelled); TF_KV(transfers_retried);
    TF_KV(bytes_requested); TF_KV(bytes_moved); TF_KV(bytes_verified); TF_KV(bytes_staged);
    TF_KV(direct_path_bytes); TF_KV(staged_path_bytes);
    TF_KV(chunk_count); TF_KV(batch_count);
    TF_KV(queue_depth); TF_KV(queue_high_water); TF_KV(active_byte_high_water);
    TF_KV(staging_usage); TF_KV(staging_high_water); TF_KV(staging_pool_hit_rate);
    TF_KV(planner_route_selections); TF_KV(planner_fallbacks);
    TF_KV(integrity_failures); TF_KV(backend_failures); TF_KV(transport_failures);
    TF_KV(retry_count); TF_KV(cancellation_count);
    TF_KV(planner_time_ns); TF_KV(queue_wait_time_ns);
    TF_KV(execution_time_ns); TF_KV(verification_time_ns);
    TF_KV(throughput_bytes_per_sec);
#undef TF_KV

    os << ",\"per_domain_bytes\":{";
    first = true;
    for (std::size_t i = 0; i < per_domain_bytes.size(); ++i) {
        if (per_domain_bytes[i] == 0) continue;
        append_key(os, to_string(static_cast<MemoryDomain>(i)), first);
        os << per_domain_bytes[i];
    }
    os << '}';

    os << ",\"per_device_bytes\":{";
    first = true;
    for (std::size_t i = 0; i < per_device_bytes.size(); ++i) {
        if (per_device_bytes[i] == 0) continue;
        append_key(os, std::to_string(i), first);
        os << per_device_bytes[i];
    }
    os << '}';

    os << ",\"per_route_class_bytes\":{";
    first = true;
    for (std::size_t i = 0; i < per_route_class_bytes.size(); ++i) {
        if (per_route_class_bytes[i] == 0) continue;
        append_key(os, to_string(static_cast<RouteClass>(i)), first);
        os << per_route_class_bytes[i];
    }
    os << '}';

    os << ",\"per_backend_bytes\":{";
    first = true;
    for (auto& [k, v] : per_backend_bytes) {
        if (!first) os << ',';
        first = false;
        std::string es;
        append_json_string(es, k.c_str());
        os << es << ':' << v;
    }
    os << '}';

    os << ",\"per_route_bytes\":{";
    first = true;
    for (auto& [k, v] : per_route_bytes) {
        if (!first) os << ',';
        first = false;
        std::string es;
        append_json_string(es, k.c_str());
        os << es << ':' << v;
    }
    os << '}';
    os << '}';
    return os.str();
}

} // namespace transfer_fabric
