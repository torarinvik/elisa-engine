#pragma once

// Bounded, generation-aware virtual file requests. The service keeps file IO
// out of the frame call site: request() only resolves and queues work, while
// pump() performs a bounded number of package reads for a worker or device
// phase. A mount generation change makes queued work stale before allocation.
#include "virtual_package.h"
#include "package_manifest.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace probe {

enum class VirtualReadState : uint8_t { Empty, Queued, Ready, Failed, Cancelled };

struct VirtualReadHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
};

class VirtualFileService {
public:
    static constexpr uint32_t MAX_REQUESTS = 32;
    static constexpr uint32_t MAX_DEPENDENCIES = 16;

    bool mount(std::filesystem::path base_root, std::vector<std::filesystem::path> overrides,
        uint64_t generation) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (generation == 0 || overrides.size() > 16) return false;
        base_root_ = std::move(base_root);
        overrides_ = std::move(overrides);
        generation_ = generation;
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

    std::future<uint32_t> pump_async(uint32_t budget = 1) {
        return std::async(std::launch::async, [this, budget]() { return pump(budget); });
    }

    bool cancel(VirtualReadHandle handle) {
        std::lock_guard<std::mutex> lock(mutex_);
        Request* item = find(handle);
        if (item == nullptr || item->state != VirtualReadState::Queued) return false;
        item->state = VirtualReadState::Cancelled;
        return true;
    }

    uint32_t pump(uint32_t budget = 1) {
        std::lock_guard<std::mutex> lock(mutex_);
        return pump_locked(budget);
    }

    VirtualReadState state(VirtualReadHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const Request* item = find(handle);
        return item == nullptr ? VirtualReadState::Empty : item->state;
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

    const std::string& error(VirtualReadHandle handle) const {
        std::lock_guard<std::mutex> lock(mutex_);
        static const std::string empty;
        const Request* item = find(handle);
        return item == nullptr ? empty : item->error;
    }

private:
    VirtualReadHandle request_locked(const std::string& logical_name, const std::string& section,
        uint64_t dependency_generation, const std::vector<std::string>* dependency_order = nullptr) {
        if (!mounted_ || !safe_package_path(logical_name) || section.empty()) return {};
        for (uint32_t slot = 0; slot < MAX_REQUESTS; ++slot) {
            Request& item = requests_[slot];
            if (item.state == VirtualReadState::Queued && item.logical_name == logical_name &&
                item.section == section && item.mount_generation == generation_ &&
                item.dependency_generation == dependency_generation &&
                item.validate_dependency_order == (dependency_order != nullptr) &&
                (dependency_order == nullptr || item.expected_dependency_order == *dependency_order)) {
                return {slot, item.generation};
            }
        }
        for (uint32_t slot = 0; slot < MAX_REQUESTS; ++slot) {
            Request& item = requests_[slot];
            if (item.state == VirtualReadState::Queued || item.state == VirtualReadState::Ready) continue;
            const uint32_t previous_generation = item.generation;
            item = {};
            item.logical_name = logical_name;
            item.section = section;
            if (dependency_order != nullptr) {
                item.expected_dependency_order = *dependency_order;
                item.validate_dependency_order = true;
            }
            item.mount_generation = generation_;
            item.dependency_generation = dependency_generation;
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
                !seen.insert(dependency).second ||
                !resolve_package_path(base_root_, overrides_, dependency, generation_).found) return false;
        }
        return true;
    }

    uint32_t pump_locked(uint32_t budget) {
        uint32_t completed = 0;
        for (Request& item : requests_) {
            if (completed >= budget) break;
            if (item.state != VirtualReadState::Queued) continue;
            if (item.mount_generation != generation_) {
                item.error = "stale mount generation";
                item.state = VirtualReadState::Failed;
                ++completed;
                continue;
            }
            if (item.dependency_generation != 0 && item.dependency_generation != generation_) {
                item.error = "stale dependency generation";
                item.state = VirtualReadState::Failed;
                ++completed;
                continue;
            }
            const PackageResolution resolved = resolve_package_path(
                base_root_, overrides_, item.logical_name, item.mount_generation);
            if (!resolved.found) {
                item.error = resolved.error;
                item.state = VirtualReadState::Failed;
                ++completed;
                continue;
            }
            std::vector<std::string> dependency_order;
            std::string dependency_error;
            if (!package_dependency_order(base_root_, overrides_, item.logical_name,
                    MAX_DEPENDENCIES, dependency_order, dependency_error) ||
                (item.validate_dependency_order && dependency_order != item.expected_dependency_order)) {
                item.error = dependency_error.empty() ? "package dependency order mismatch" : dependency_error;
                item.state = VirtualReadState::Failed;
                ++completed;
                continue;
            }
            const BinaryPackageIndex index = read_binary_package_index(resolved.path);
            if (!index.valid || !read_binary_package_section(resolved.path, index, item.section,
                    item.bytes, item.error)) {
                if (item.error.empty()) item.error = index.error;
                item.state = VirtualReadState::Failed;
            } else {
                item.state = VirtualReadState::Ready;
            }
            ++completed;
        }
        return completed;
    }
    struct Request {
        std::string logical_name;
        std::string section;
        std::vector<uint8_t> bytes;
        std::vector<std::string> expected_dependency_order;
        std::string error;
        uint64_t mount_generation = 0;
        uint64_t dependency_generation = 0;
        uint32_t generation = 0;
        VirtualReadState state = VirtualReadState::Empty;
        bool validate_dependency_order = false;
    };

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
    bool mounted_ = false;
    mutable std::mutex mutex_;
    std::array<Request, MAX_REQUESTS> requests_{};
};

} // namespace probe
