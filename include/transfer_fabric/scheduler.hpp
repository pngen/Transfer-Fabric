#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "transfer_fabric/export.hpp"
#include "transfer_fabric/transfer.hpp"
#include "transfer_fabric/errors.hpp"

namespace transfer_fabric {

// Bounded, priority-aware task scheduler. It owns a fixed worker pool and a
// bounded set of multi-level queues. No busy-spin, no unbounded queues, and it
// is shut down deterministically (idle, running, or queued).
class TF_API Scheduler {
public:
    struct Task {
        std::function<void()> body;
        Priority priority{Priority::normal};
        std::string name;
    };

    explicit Scheduler(std::size_t workers, std::size_t max_queued);
    ~Scheduler();
    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    void start();
    // Returns false if the scheduler is shutting down or the queue is full.
    bool submit(Task task);
    void shutdown();

    std::size_t worker_count() const noexcept { return workers_.size(); }
    std::size_t queue_depth() const noexcept { return queue_depth_.load(std::memory_order_relaxed); }
    std::size_t max_queued() const noexcept { return max_queued_; }
    bool is_shutting_down() const noexcept { return shutting_down_.load(std::memory_order_relaxed); }

private:
    void worker_loop(std::size_t idx);
    bool pop(Task& out);

    std::size_t max_queued_;
    std::mutex mu_;
    std::condition_variable cv_;
    std::atomic<bool> shutting_down_{false};
    std::atomic<std::size_t> queue_depth_{0};

    static constexpr int kPriorityBands = 10;
    std::deque<Task> queues_[kPriorityBands];
    int max_band_{0};

    std::vector<std::thread> workers_;
};

} // namespace transfer_fabric
