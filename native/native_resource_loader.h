#pragma once

// Native half of the bounded asset-loader contract. File work is delegated to
// VirtualFileService; only the caller's pump performs decode and GPU upload.
#include "virtual_file_service.h"
#include "wiGraphicsDevice.h"

#include <array>
#include <cstdint>
#include <future>
#include <string>
#include <vector>

namespace probe {

enum class NativeAssetState : uint8_t { Empty, Queued, Decoding, Uploading, Resident, Failed, Cancelled };

struct NativeAssetHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
};

struct NativeAssetTelemetry {
    uint32_t requested = 0;
    uint32_t coalesced = 0;
    uint32_t decoded = 0;
    uint32_t uploaded = 0;
    uint32_t cancelled = 0;
    uint32_t failed = 0;
};

class NativeResourceLoader {
public:
    static constexpr uint32_t MAX_ASSETS = 16;

    explicit NativeResourceLoader(wi::graphics::GraphicsDevice* device) : device_(device) {}

    bool mount(const std::filesystem::path& base, std::vector<std::filesystem::path> overrides,
        uint64_t generation) { return files_.mount(base, std::move(overrides), generation); }

    NativeAssetHandle request(const std::string& logical_name, const std::string& section,
        uint64_t dependency_generation = 0) {
        for (uint32_t slot = 0; slot < MAX_ASSETS; ++slot) {
            Item& item = items_[slot];
            if (item.state != NativeAssetState::Empty && item.logical_name == logical_name &&
                item.section == section && item.dependency_generation == dependency_generation) {
                ++telemetry_.coalesced;
                return {slot, item.generation};
            }
        }
        for (uint32_t slot = 0; slot < MAX_ASSETS; ++slot) {
            Item& item = items_[slot];
            if (item.state != NativeAssetState::Empty) continue;
            const uint32_t previous = item.generation;
            item = {};
            item.generation = previous == UINT32_MAX ? 0 : previous + 1;
            if (item.generation == 0) { item.state = NativeAssetState::Failed; return {}; }
            item.logical_name = logical_name;
            item.section = section;
            item.dependency_generation = dependency_generation;
            item.file = files_.request(logical_name, section, dependency_generation);
            if (item.file.generation == 0) { item.state = NativeAssetState::Failed; ++telemetry_.failed; return {}; }
            item.state = NativeAssetState::Queued;
            ++telemetry_.requested;
            return {slot, item.generation};
        }
        return {};
    }

    bool cancel(NativeAssetHandle handle) {
        Item* item = find(handle);
        if (item == nullptr || item->state != NativeAssetState::Queued) return false;
        files_.cancel(item->file);
        item->state = NativeAssetState::Cancelled;
        ++telemetry_.cancelled;
        return true;
    }

    std::future<uint32_t> pump_io_async(uint32_t io_budget = 1) {
        return files_.pump_async(io_budget);
    }

    uint32_t pump(uint32_t io_budget = 1, uint32_t upload_budget = 1) {
        files_.pump(io_budget);
        return upload_ready(upload_budget);
    }

    uint32_t upload_ready(uint32_t upload_budget = 1) {
        uint32_t uploaded = 0;
        for (Item& item : items_) {
            if (item.state != NativeAssetState::Queued) continue;
            const VirtualReadState file_state = files_.state(item.file);
            if (file_state == VirtualReadState::Failed || file_state == VirtualReadState::Cancelled) {
                item.error = files_.error(item.file);
                item.state = file_state == VirtualReadState::Cancelled ? NativeAssetState::Cancelled : NativeAssetState::Failed;
                if (item.state == NativeAssetState::Failed) ++telemetry_.failed;
                continue;
            }
            if (file_state != VirtualReadState::Ready || uploaded >= upload_budget) continue;
            item.state = NativeAssetState::Decoding;
            uint64_t generation = 0;
            if (!files_.take(item.file, item.bytes, generation, item.error)) {
                item.state = NativeAssetState::Failed;
                ++telemetry_.failed;
                continue;
            }
            ++telemetry_.decoded;
            item.state = NativeAssetState::Uploading;
            if (!upload(item)) {
                item.state = NativeAssetState::Failed;
                ++telemetry_.failed;
                continue;
            }
            item.state = NativeAssetState::Resident;
            ++telemetry_.uploaded;
            ++uploaded;
        }
        return uploaded;
    }

    NativeAssetState state(NativeAssetHandle handle) const {
        const Item* item = find(handle);
        return item == nullptr ? NativeAssetState::Empty : item->state;
    }

    const wi::graphics::Texture* texture(NativeAssetHandle handle) const {
        const Item* item = find(handle);
        return item != nullptr && item->state == NativeAssetState::Resident ? &item->texture : nullptr;
    }

    const NativeAssetTelemetry& telemetry() const { return telemetry_; }

private:
    struct Item {
        std::string logical_name;
        std::string section;
        std::vector<uint8_t> bytes;
        std::string error;
        VirtualReadHandle file;
        uint64_t dependency_generation = 0;
        uint32_t generation = 0;
        NativeAssetState state = NativeAssetState::Empty;
        wi::graphics::Texture texture;
    };

    Item* find(NativeAssetHandle handle) {
        return handle.slot < MAX_ASSETS && items_[handle.slot].generation == handle.generation &&
            handle.generation != 0 ? &items_[handle.slot] : nullptr;
    }
    const Item* find(NativeAssetHandle handle) const {
        return handle.slot < MAX_ASSETS && items_[handle.slot].generation == handle.generation &&
            handle.generation != 0 ? &items_[handle.slot] : nullptr;
    }
    bool upload(Item& item) {
        if (device_ == nullptr || item.bytes.empty()) return false;
        const uint32_t pixel = static_cast<uint32_t>(item.bytes[0]) |
            (static_cast<uint32_t>(item.bytes[item.bytes.size() > 1 ? 1 : 0]) << 8) |
            (static_cast<uint32_t>(item.bytes[item.bytes.size() > 2 ? 2 : 0]) << 16) | 0xFF000000u;
        wi::graphics::TextureDesc desc;
        desc.width = desc.height = desc.depth = desc.array_size = desc.mip_levels = desc.sample_count = 1;
        desc.format = wi::graphics::Format::R8G8B8A8_UNORM;
        desc.bind_flags = wi::graphics::BindFlag::SHADER_RESOURCE;
        wi::graphics::SubresourceData data;
        data.data_ptr = &pixel;
        data.row_pitch = data.slice_pitch = sizeof(pixel);
        return device_->CreateTexture(&desc, &data, &item.texture) && item.texture.IsValid();
    }

    VirtualFileService files_;
    wi::graphics::GraphicsDevice* device_ = nullptr;
    std::array<Item, MAX_ASSETS> items_{};
    NativeAssetTelemetry telemetry_{};
};

} // namespace probe
