#include "transfer_fabric/runtime.hpp"
#include "internal.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "transfer_fabric/backends/host.hpp"
#include "transfer_fabric/backends/file.hpp"
#include "transfer_fabric/backends/shm.hpp"
#include "transfer_fabric/backends/cuda.hpp"
#include "transfer_fabric/platform/cuda_detect.hpp"

#if defined(_WIN32)
#  include <windows.h>
#endif

namespace transfer_fabric {

using namespace std::chrono;

// ========================================================================
// EndpointRegistry
// ========================================================================
EndpointRegistry::EndpointRegistry() = default;

Error EndpointRegistry::allocate(std::shared_ptr<EndpointRecord> rec, EndpointHandle& out) {
    std::lock_guard<std::mutex> g(mu_);
    std::size_t index;
    if (!free_.empty()) {
        index = free_.back(); free_.pop_back();
    } else {
        index = slots_.size();
        slots_.emplace_back();
    }
    Slot& s = slots_[index];
    ++s.gen;
    if (s.gen == 0) ++s.gen; // avoid generation 0
    s.used = true;
    s.rec = rec;
    rec->generation = s.gen;
    out.slot = index + 1;            // 1-based
    out.generation = s.gen;
    return Error();
}

std::shared_ptr<EndpointRecord> EndpointRegistry::get(EndpointHandle h) const {
    std::lock_guard<std::mutex> g(mu_);
    if (h.slot == 0 || h.slot > slots_.size()) return nullptr;
    const Slot& s = slots_[h.slot - 1];
    if (!s.used || s.gen != h.generation) return nullptr;
    return s.rec;
}

Error EndpointRegistry::remove(EndpointHandle h) {
    std::lock_guard<std::mutex> g(mu_);
    if (h.slot == 0 || h.slot > slots_.size()) return Error(ErrorCategory::invalid_request, "bad slot");
    Slot& s = slots_[h.slot - 1];
    if (!s.used || s.gen != h.generation) return Error(ErrorCategory::stale_generation, "stale endpoint handle");
    ++s.gen;
    if (s.gen == 0) ++s.gen;
    s.used = false;
    s.rec.reset();
    free_.push_back(h.slot - 1);
    return Error();
}

std::size_t EndpointRegistry::size() const {
    std::lock_guard<std::mutex> g(mu_);
    return slots_.size();
}
void EndpointRegistry::clear() {
    std::lock_guard<std::mutex> g(mu_);
    slots_.clear();
    free_.clear();
}

// ========================================================================
// TransferRegistry
// ========================================================================
std::shared_ptr<TransferRecord> TransferRegistry::insert(const TransferId& id, std::shared_ptr<TransferRecord> rec) {
    std::lock_guard<std::mutex> g(mu_);
    map_[id] = rec;
    return rec;
}
std::shared_ptr<TransferRecord> TransferRegistry::find(const TransferId& id) const {
    std::lock_guard<std::mutex> g(mu_);
    auto it = map_.find(id);
    return it == map_.end() ? nullptr : it->second;
}
bool TransferRegistry::erase(const TransferId& id) {
    std::lock_guard<std::mutex> g(mu_);
    return map_.erase(id) != 0;
}
std::size_t TransferRegistry::size() const {
    std::lock_guard<std::mutex> g(mu_);
    return map_.size();
}
std::vector<std::shared_ptr<TransferRecord>> TransferRegistry::all() const {
    std::lock_guard<std::mutex> g(mu_);
    std::vector<std::shared_ptr<TransferRecord>> out;
    out.reserve(map_.size());
    for (auto& [k, v] : map_) out.push_back(v);
    return out;
}
std::size_t TransferRegistry::live_count() const {
    std::lock_guard<std::mutex> g(mu_);
    std::size_t n = 0;
    for (auto& [k, v] : map_) {
        TransferState st;
        {
            std::lock_guard<std::mutex> lg(v->mtx);
            st = v->status.state;
        }
        if (!is_terminal(st)) ++n;
    }
    return n;
}

} // namespace transfer_fabric
