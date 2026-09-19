#pragma once

// Native execution of Elisa-approved schedule waves. Tasks in one wave may
// overlap; the next wave cannot begin until every task in the prior wave joins.
#include "probe_core.h"

#include <atomic>
#include <chrono>
#include <cstddef>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <vector>

namespace probe {

class WaveBarrier {
public:
    explicit WaveBarrier(size_t participants) : participants_(participants) {}
    void arrive_and_wait() {
        std::unique_lock<std::mutex> guard(lock_);
        const size_t generation = generation_;
        if (++arrived_ == participants_) {
            arrived_ = 0;
            ++generation_;
            ready_.notify_all();
            return;
        }
        ready_.wait(guard, [&] { return generation_ != generation; });
    }

private:
    const size_t participants_;
    size_t arrived_ = 0;
    size_t generation_ = 0;
    std::mutex lock_;
    std::condition_variable ready_;
};

class ParallelExecutor {
public:
    using Task = std::function<void()>;
    using Wave = std::vector<Task>;

    bool run(const std::vector<Wave>& waves, size_t worker_limit) const {
        if (worker_limit == 0) return false;
        for (const Wave& wave : waves) {
            if (wave.empty() || wave.size() > worker_limit) return false;
            std::atomic<bool> failed = false;
            WaveBarrier gate(wave.size() + 1);
            std::vector<std::thread> workers;
            workers.reserve(wave.size());
            for (const Task& task : wave) {
                workers.emplace_back([&gate, &failed, &task] {
                    gate.arrive_and_wait();
                    try { task(); } catch (...) { failed.store(true); }
                });
            }
            gate.arrive_and_wait();
            for (std::thread& worker : workers) worker.join();
            if (failed.load()) return false;
        }
        return true;
    }
};

inline bool probe_parallel_executor() {
    ParallelExecutor executor;
    std::atomic<int> active = 0;
    std::atomic<int> maximum = 0;
    std::mutex lock;
    std::vector<int> order;
    auto task = [&](int marker) {
        const int now = active.fetch_add(1) + 1;
        int observed = maximum.load();
        while (now > observed && !maximum.compare_exchange_weak(observed, now)) {}
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        {
            std::lock_guard<std::mutex> guard(lock);
            order.push_back(marker);
        }
        active.fetch_sub(1);
    };
    std::vector<ParallelExecutor::Wave> waves;
    waves.push_back({[&] { task(1); }, [&] { task(2); }});
    waves.push_back({[&] { task(3); }});
    if (!check(executor.run(waves, 2), "parallel executor completes waves") ||
        !check(maximum.load() == 2, "parallel executor overlaps independent tasks") ||
        !check(order.size() == 3 && order[2] == 3, "parallel executor enforces wave barrier") ||
        !check(!executor.run(waves, 1), "parallel executor enforces worker cap")) return false;
    std::vector<ParallelExecutor::Wave> failing;
    failing.push_back({[] { throw 1; }});
    return check(!executor.run(failing, 1), "parallel executor propagates task failure");
}

} // namespace probe
