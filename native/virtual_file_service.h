#pragma once

// Bounded, generation-aware virtual file requests. request() validates and
// queues work; pump() claims a bounded number of requests and performs their
// package IO without holding the service lock. A mount generation change
// makes queued work stale before allocation and in-flight results stale before
// publication.
#include "virtual_package.h"
#include "package_manifest.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <filesystem>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

namespace probe {

enum class VirtualReadState : uint8_t { Empty, Queued, Reading, Ready, Failed, Cancelled };

struct VirtualReadCancellation {
    std::atomic_bool requested{false};
    std::atomic_size_t bytes_read{0};
};

struct VirtualReadHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
};

class VirtualFileService {
public:
    static constexpr uint32_t MAX_REQUESTS = 32;
    static constexpr uint32_t MAX_DEPENDENCIES = 16;
    static constexpr uint32_t ASYNC_WORKERS = 2;
    static constexpr size_t MAX_PENDING_PUMPS = 32;

    VirtualFileService() {
        try {
            for (uint32_t worker = 0; worker < ASYNC_WORKERS; ++worker) {
                workers_.emplace_back([this]() { worker_loop(); });
            }
        } catch (...) {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_workers_ = true;
            }
            worker_condition_.notify_all();
            for (std::thread& worker : workers_) {
                if (worker.joinable()) worker.join();
            }
            throw;
        }
    }

    ~VirtualFileService() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_workers_ = true;
        }
        worker_condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    bool mount(std::filesystem::path base_root, std::vector<std::filesystem::path> overrides,
        uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == 0 || overrides.size() > 16 || mount_epoch_ == UINT64_MAX) return false;
        base_root_ = std::move(base_root);
        overrides_ = std::move(overrides);
        generation_ = generation;
        ++mount_epoch_;
        mounted_ = true;
        return true;
    }

    VirtualReadHandle request(const std::string& logical_name, const std::string& section,
        uint64_t dependency_generation = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        return request_locked(logical_name, section, dependency_generation);
    }

    VirtualReadHandle request_with_dependencies(const std::string& logical_name,
        const std::string& section, const std::vector<std::string>& dependencies,
        uint64_t dependency_generation = 0) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!dependency_names_valid_locked(logical_name, dependencies)) return {};
        return request_locked(logical_name, section, dependency_generation, &dependencies);
    }

    // Returns zero without queueing when shutdown has started or the bounded
    // async pump queue is full.
    std::future<uint32_t> pump_async(uint32_t budget = 1) {
        AsyncPump task;
        task.budget = budget;
        std::future<uint32_t> result = task.completion.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (stopping_workers_ || pending_pumps_.size() >= MAX_PENDING_PUMPS) {
                task.completion.set_value(0);
                return result;
            }
            pending_pumps_.push_back(std::move(task));
        }
        worker_condition_.notify_one();
        return result;
    }

    bool cancel(VirtualReadHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        Request* item = find(handle);
        if (item == nullptr || (item->state != VirtualReadState::Queued &&
                item->state != VirtualReadState::Reading) || !item->cancellation) return false;
        item->cancellation->requested.store(true, std::memory_order_relaxed);
        item->state = VirtualReadState::Cancelled;
        return true;
    }

    uint32_t pump(uint32_t budget = 1) {
        uint32_t completed = 0;
        while (completed < budget) {
            Work work;
            bool claimed = false;
            bool settled_stale = false;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                for (uint32_t slot = 0; slot < MAX_REQUESTS; ++slot) {
                    Request& item = requests_[slot];
                    if (item.state != VirtualReadState::Queued) continue;
                    if (item.mount_epoch != mount_epoch_ || item.mount_generation != generation_) {
                        item.error = "stale mount generation";
                        item.state = VirtualReadState::Failed;
                        settled_stale = true;
                        break;
                    }
                    if (item.dependency_generation != 0 && item.dependency_generation != generation_) {
                        item.error = "stale dependency generation";
                        item.state = VirtualReadState::Failed;
                        settled_stale = true;
                        break;
                    }
                    item.state = VirtualReadState::Reading;
                    work.handle = {slot, item.generation};
                    work.logical_name = item.logical_name;
                    work.section = item.section;
                    work.expected_dependency_order = item.expected_dependency_order;
                    work.mount_generation = item.mount_generation;
                    work.mount_epoch = item.mount_epoch;
                    work.validate_dependency_order = item.validate_dependency_order;
                    work.cancellation = item.cancellation;
                    work.base_root = base_root_;
                    work.overrides = overrides_;
                    claimed = true;
                    break;
                }
            }
            if (settled_stale) {
                ++completed;
                continue;
            }
            if (!claimed) break;

            std::vector<uint8_t> bytes;
            std::string error;
            if (work.cancellation && work.cancellation->requested.load(std::memory_order_relaxed)) {
                ++completed;
                continue;
            }
            const PackageResolution resolved = resolve_package_path(
                work.base_root, work.overrides, work.logical_name, work.mount_generation);
            bool succeeded = false;
            if (!resolved.found) {
                error = resolved.error;
            } else {
                std::vector<std::string> dependency_order;
                std::string dependency_error;
                if (!package_dependency_order(work.base_root, work.overrides, work.logical_name,
                        MAX_DEPENDENCIES, dependency_order, dependency_error) ||
                    (work.validate_dependency_order && dependency_order != work.expected_dependency_order)) {
                    error = dependency_error.empty() ? "package dependency order mismatch" : dependency_error;
                } else if (work.cancellation &&
                    work.cancellation->requested.load(std::memory_order_relaxed)) {
                    ++completed;
                    continue;
                } else {
                    const BinaryPackageIndex index = read_binary_package_index(resolved.path);
                    succeeded = index.valid && read_binary_package_section(
                        resolved.path, index, work.section, bytes, error,
                        [cancellation = work.cancellation](size_t bytes_read, size_t) {
                            if (!cancellation) return false;
                            cancellation->bytes_read.store(bytes_read, std::memory_order_relaxed);
                            return cancellation->requested.load(std::memory_order_relaxed);
                        });
                    if (!succeeded && error.empty()) error = index.error;
                }
            }

            {
                std::lock_guard<std::mutex> lock(mutex_);
                Request* item = find(work.handle);
                if (item != nullptr && item->state == VirtualReadState::Reading) {
                    if (item->mount_epoch != mount_epoch_ || item->mount_generation != generation_ ||
                        item->mount_generation != work.mount_generation) {
                        item->error = "stale mount generation";
                        item->state = VirtualReadState::Failed;
                    } else if (item->dependency_generation != 0 &&
                        item->dependency_generation != generation_) {
                        item->error = "stale dependency generation";
                        item->state = VirtualReadState::Failed;
                    } else if (!succeeded) {
                        item->error = std::move(error);
                        item->state = VirtualReadState::Failed;
                    } else {
                        item->bytes = std::move(bytes);
                        item->state = VirtualReadState::Ready;
                    }
                }
            }
            // Cancellation and slot reuse do not publish the abandoned result,
            // but the bounded work item still counts against this pump budget.
            ++completed;
        }
        return completed;
    }

    VirtualReadState state(VirtualReadHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request* item = find(handle);
        return item == nullptr ? VirtualReadState::Empty : item->state;
    }

    size_t bytes_read(VirtualReadHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request* item = find(handle);
        return item == nullptr || !item->cancellation ? 0 :
            item->cancellation->bytes_read.load(std::memory_order_relaxed);
    }

    bool take(VirtualReadHandle handle, std::vector<uint8_t>& output, uint64_t& generation,
        std::string& error) {
        std::lock_guard<std::mutex> lock(mutex_);
        Request* item = find(handle);
        if (item == nullptr) { error = "invalid read handle"; return false; }
        if (item->state != VirtualReadState::Ready) {
            error = item->error.empty() ? "read is not ready" : item->error;
            return false;
        }
        output = std::move(item->bytes);
        generation = item->mount_generation;
        item->state = VirtualReadState::Empty;
        return true;
    }

    std::string error(VirtualReadHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request* item = find(handle);
        return item == nullptr ? std::string{} : item->error;
    }

private:
    VirtualReadHandle request_locked(const std::string& logical_name, const std::string& section,
        uint64_t dependency_generation, const std::vector<std::string>* dependency_order = nullptr) {
        if (!mounted_ || !safe_package_path(logical_name) || section.empty()) return {};
        for (uint32_t slot = 0; slot < MAX_REQUESTS; ++slot) {
            Request& item = requests_[slot];
            if ((item.state == VirtualReadState::Queued || item.state == VirtualReadState::Reading) &&
                item.logical_name == logical_name &&
                item.section == section && item.mount_generation == generation_ &&
                item.mount_epoch == mount_epoch_ &&
                item.dependency_generation == dependency_generation &&
                item.validate_dependency_order == (dependency_order != nullptr) &&
                (dependency_order == nullptr || item.expected_dependency_order == *dependency_order)) {
                return {slot, item.generation};
            }
        }
        for (uint32_t slot = 0; slot < MAX_REQUESTS; ++slot) {
            Request& item = requests_[slot];
            if (item.state == VirtualReadState::Queued || item.state == VirtualReadState::Reading ||
                item.state == VirtualReadState::Ready) continue;
            std::shared_ptr<VirtualReadCancellation> cancellation;
            try {
                cancellation = std::make_shared<VirtualReadCancellation>();
            } catch (...) {
                return {};
            }
            const uint32_t previous_generation = item.generation;
            item = {};
            item.logical_name = logical_name;
            item.section = section;
            if (dependency_order != nullptr) {
                item.expected_dependency_order = *dependency_order;
                item.validate_dependency_order = true;
            }
            item.mount_generation = generation_;
            item.mount_epoch = mount_epoch_;
            item.dependency_generation = dependency_generation;
            item.cancellation = std::move(cancellation);
            item.generation = previous_generation == UINT32_MAX ? 0 : previous_generation + 1;
            if (item.generation == 0) { item.state = VirtualReadState::Failed; return {}; }
            item.state = VirtualReadState::Queued;
            return {slot, item.generation};
        }
        return {};
    }

    bool dependency_names_valid_locked(const std::string& logical_name,
        const std::vector<std::string>& dependencies) const {
        if (dependencies.size() > MAX_DEPENDENCIES) return false;
        std::set<std::string> seen;
        for (const std::string& dependency : dependencies) {
            if (!safe_package_path(dependency) || dependency == logical_name ||
                !seen.insert(dependency).second) return false;
        }
        return true;
    }

    struct Work {
        VirtualReadHandle handle;
        std::string logical_name;
        std::string section;
        std::vector<std::string> expected_dependency_order;
        std::shared_ptr<VirtualReadCancellation> cancellation;
        std::filesystem::path base_root;
        std::vector<std::filesystem::path> overrides;
        uint64_t mount_generation = 0;
        uint64_t mount_epoch = 0;
        bool validate_dependency_order = false;
    };
    struct Request {
        std::string logical_name;
        std::string section;
        std::vector<uint8_t> bytes;
        std::vector<std::string> expected_dependency_order;
        std::string error;
        uint64_t mount_generation = 0;
        uint64_t mount_epoch = 0;
        uint64_t dependency_generation = 0;
        uint32_t generation = 0;
        VirtualReadState state = VirtualReadState::Empty;
        bool validate_dependency_order = false;
        std::shared_ptr<VirtualReadCancellation> cancellation;
    };

    struct AsyncPump {
        uint32_t budget = 0;
        std::promise<uint32_t> completion;
    };

    void worker_loop() {
        for (;;) {
            AsyncPump task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                worker_condition_.wait(lock, [this]() {
                    return stopping_workers_ || !pending_pumps_.empty();
                });
                if (stopping_workers_ && pending_pumps_.empty()) return;
                task = std::move(pending_pumps_.front());
                pending_pumps_.pop_front();
            }
            try {
                task.completion.set_value(pump(task.budget));
            } catch (...) {
                task.completion.set_exception(std::current_exception());
            }
        }
    }

    Request* find(VirtualReadHandle handle) {
        return handle.slot < MAX_REQUESTS && requests_[handle.slot].generation == handle.generation &&
                handle.generation != 0 ? &requests_[handle.slot] : nullptr;
    }

    const Request* find(VirtualReadHandle handle) const {
        return handle.slot < MAX_REQUESTS && requests_[handle.slot].generation == handle.generation &&
                handle.generation != 0 ? &requests_[handle.slot] : nullptr;
    }

    std::filesystem::path base_root_;
    std::vector<std::filesystem::path> overrides_;
    uint64_t generation_ = 0;
    uint64_t mount_epoch_ = 0;
    bool mounted_ = false;
    mutable std::mutex mutex_;
    std::condition_variable worker_condition_;
    std::deque<AsyncPump> pending_pumps_;
    std::vector<std::thread> workers_;
    bool stopping_workers_ = false;
    std::array<Request, MAX_REQUESTS> requests_{};
};

} // namespace probe
