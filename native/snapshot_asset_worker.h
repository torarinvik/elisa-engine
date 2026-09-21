#pragma once

// One background thread that runs bounded asset jobs away from the owner
// thread. Jobs are identified by a caller-chosen serial. Finished results wait
// in a done list until the owner takes them, so the owner decides when a
// result becomes visible. Cancelling a job drops it wherever it is: a queued
// job never starts, a running job's result is discarded when it returns, and
// a finished result is removed before the owner can take it. A job that is
// already inside an OS read finishes that read first.
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

namespace elisa::assets {

template <typename Result>
class SerialJobWorker {
public:
    static constexpr size_t MAX_JOBS = 64;
    using Job = std::function<Result()>;

    SerialJobWorker() = default;
    SerialJobWorker(const SerialJobWorker&) = delete;
    SerialJobWorker& operator=(const SerialJobWorker&) = delete;
    ~SerialJobWorker() { stop(); }

    // False for serial 0, a serial already queued, running or finished, or a
    // full queue. Higher priorities start first; equal priorities stay FIFO.
    bool submit(uint64_t serial, Job job) {
        return submit(serial, 0, std::move(job));
    }

    bool submit(uint64_t serial, int32_t priority, Job job) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (serial == 0 || !job || stopping_ || tracked_unlocked(serial) ||
            queue_.size() + done_.size() + (running_ != 0 ? 1 : 0) >= MAX_JOBS) return false;
        queue_.push_back({serial, priority, std::move(job)});
        order_queue_unlocked();
        if (!thread_.joinable()) thread_ = std::thread([this] { run(); });
        wake_.notify_all();
        return true;
    }

    // A queued request can change priority; a running or finished job cannot.
    bool set_priority(uint64_t serial, int32_t priority) {
        std::lock_guard<std::mutex> guard(mutex_);
        for (QueuedJob& job : queue_) {
            if (job.serial != serial) continue;
            job.priority = priority;
            order_queue_unlocked();
            wake_.notify_all();
            return true;
        }
        return false;
    }

    // True when the serial was queued, running or finished and not yet taken.
    bool cancel(uint64_t serial) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (serial == 0) return false;
        if (running_ == serial) {
            running_cancelled_ = true;
            return true;
        }
        for (auto job = queue_.begin(); job != queue_.end(); ++job) {
            if (job->serial == serial) {
                queue_.erase(job);
                return true;
            }
        }
        for (auto result = done_.begin(); result != done_.end(); ++result) {
            if (result->first == serial) {
                done_.erase(result);
                return true;
            }
        }
        return false;
    }

    // Moves up to `budget` finished results into `out`, oldest first.
    size_t take(size_t budget, std::vector<std::pair<uint64_t, Result>>& out) {
        std::lock_guard<std::mutex> guard(mutex_);
        size_t taken = 0;
        while (taken < budget && !done_.empty()) {
            out.push_back(std::move(done_.front()));
            done_.pop_front();
            ++taken;
        }
        return taken;
    }

    // Drops every job and result, releases test holds and joins the thread.
    // The worker accepts jobs again afterwards.
    void stop() {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            stopping_ = true;
            queue_.clear();
            done_.clear();
            hold_start_ = false;
            hold_running_ = false;
        }
        wake_.notify_all();
        if (thread_.joinable()) thread_.join();
        std::lock_guard<std::mutex> guard(mutex_);
        done_.clear();
        running_ = 0;
        running_cancelled_ = false;
        stopping_ = false;
    }

    // A job calls this between its IO steps. It waits while the running hold
    // is set and returns false once the job is cancelled or the worker stops,
    // so the job can return without its remaining reads.
    bool checkpoint() {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || !hold_running_; });
        return !stopping_ && !running_cancelled_;
    }

    // Test hooks. A start hold keeps queued jobs from starting; a running hold
    // parks a started job at its next checkpoint.
    void hold(bool start, bool running) {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            hold_start_ = start;
            hold_running_ = running;
        }
        wake_.notify_all();
    }

    bool wait_until(uint64_t started, uint64_t finished, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return progress_.wait_for(lock, timeout, [&] { return started_ >= started && finished_ >= finished; });
    }

    uint64_t started() {
        std::lock_guard<std::mutex> guard(mutex_);
        return started_;
    }

    uint64_t finished() {
        std::lock_guard<std::mutex> guard(mutex_);
        return finished_;
    }

    // Jobs queued, running or finished but not taken.
    size_t outstanding() {
        std::lock_guard<std::mutex> guard(mutex_);
        return queue_.size() + done_.size() + (running_ != 0 ? 1 : 0);
    }

private:
    struct QueuedJob {
        uint64_t serial = 0;
        int32_t priority = 0;
        Job callback;
    };

    void order_queue_unlocked() {
        std::stable_sort(queue_.begin(), queue_.end(), [](const QueuedJob& left, const QueuedJob& right) {
            return left.priority > right.priority;
        });
    }

    bool tracked_unlocked(uint64_t serial) const {
        if (running_ == serial) return true;
        for (const auto& job : queue_) if (job.serial == serial) return true;
        for (const auto& result : done_) if (result.first == serial) return true;
        return false;
    }

    void run() {
        std::unique_lock<std::mutex> lock(mutex_);
        for (;;) {
            wake_.wait(lock, [this] { return stopping_ || (!queue_.empty() && !hold_start_); });
            if (stopping_) return;
            QueuedJob job = std::move(queue_.front());
            queue_.pop_front();
            running_ = job.serial;
            running_cancelled_ = false;
            ++started_;
            progress_.notify_all();
            lock.unlock();
            // A job that throws posts a default-constructed result.
            Result result{};
            try {
                result = job.callback();
            } catch (...) {
                result = Result{};
            }
            lock.lock();
            if (!running_cancelled_ && !stopping_) done_.emplace_back(job.serial, std::move(result));
            running_ = 0;
            running_cancelled_ = false;
            ++finished_;
            progress_.notify_all();
        }
    }

    std::mutex mutex_;
    std::condition_variable wake_;
    std::condition_variable progress_;
    std::deque<QueuedJob> queue_;
    std::deque<std::pair<uint64_t, Result>> done_;
    std::thread thread_;
    uint64_t running_ = 0;
    uint64_t started_ = 0;
    uint64_t finished_ = 0;
    bool running_cancelled_ = false;
    bool stopping_ = false;
    bool hold_start_ = false;
    bool hold_running_ = false;
};

} // namespace elisa::assets
