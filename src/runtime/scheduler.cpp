#include "transfer_fabric/scheduler.hpp"

namespace transfer_fabric {

Scheduler::Scheduler(std::size_t workers, std::size_t max_queued)
    : max_queued_(max_queued ? max_queued : 1) {
    max_band_ = -1;
    workers_.reserve(workers);
    for (std::size_t i = 0; i < workers; ++i)
        workers_.emplace_back(&Scheduler::worker_loop, this, i);
}

Scheduler::~Scheduler() { shutdown(); }

void Scheduler::start() {
    // Workers are created in the constructor; they wait on the queue.
}

bool Scheduler::submit(Task task) {
    std::lock_guard<std::mutex> g(mu_);
    if (shutting_down_.load(std::memory_order_relaxed)) return false;
    if (queue_depth_.load(std::memory_order_relaxed) >= max_queued_) return false;
    int band = static_cast<int>(task.priority);
    if (band < 0) band = 0;
    if (band >= kPriorityBands) band = kPriorityBands - 1;
    queues_[band].push_back(std::move(task));
    queue_depth_.fetch_add(1, std::memory_order_relaxed);
    if (band > max_band_) max_band_ = band;
    cv_.notify_all();
    return true;
}

// Precondition: mu_ is held by the caller.
bool Scheduler::pop(Task& out) {
    if (max_band_ < 0) return false;
    while (max_band_ >= 0 && queues_[max_band_].empty()) --max_band_;
    if (max_band_ < 0) return false;
    out = std::move(queues_[max_band_].front());
    queues_[max_band_].pop_front();
    queue_depth_.fetch_sub(1, std::memory_order_relaxed);
    if (queues_[max_band_].empty()) --max_band_;
    return true;
}

void Scheduler::worker_loop(std::size_t) {
    while (true) {
        Task t;
        bool got = false;
        {
            // Predicate-based wait: cannot lose a wakeup, and it re-checks after
            // every spurious wakeup. queue_depth_ is the authoritative work count.
            std::unique_lock<std::mutex> g(mu_);
            cv_.wait(g, [&] {
                return shutting_down_.load(std::memory_order_relaxed)
                    || queue_depth_.load(std::memory_order_relaxed) > 0;
            });
            if (shutting_down_.load(std::memory_order_relaxed)
                && queue_depth_.load(std::memory_order_relaxed) == 0) {
                return;  // drained and shutting down
            }
            got = pop(t);
        }
        if (got && t.body) t.body();
    }
}

void Scheduler::shutdown() {
    bool join = false;
    {
        std::lock_guard<std::mutex> g(mu_);
        if (!shutting_down_.load(std::memory_order_relaxed)) {
            shutting_down_.store(true, std::memory_order_relaxed);
            join = true;
        }
        cv_.notify_all();
    }
    if (!join) {
        return;
    }
    for (auto& w : workers_) {
        if (w.joinable()) w.join();
    }
    workers_.clear();
}

} // namespace transfer_fabric
