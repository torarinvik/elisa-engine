#pragma once

#include "wiGraphics.h"
#include "wiRenderPath3D.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>

namespace elisa::render_graph {

constexpr uint32_t MAX_RESOURCES = 64;
constexpr uint32_t MAX_PASSES = 32;
constexpr uint32_t MAX_TARGETS = 64;
constexpr uint32_t NO_TARGET = std::numeric_limits<uint32_t>::max();

enum class SizeMode : int32_t { Fixed = 0, PrimaryInternal = 1 };
enum class Format : int32_t { Rgba8 = 0, Rgba16Float = 1, Depth32 = 2, R11G11B10Float = 3 };
enum class Lifetime : int32_t { Imported = 0, Persistent = 1, Transient = 2 };
enum class Operation : int32_t { ClearColor = 0, CopyColor = 1 };

struct Resource {
    uint32_t id = 0;
    SizeMode size_mode = SizeMode::Fixed;
    uint32_t width = 0;
    uint32_t height = 0;
    Format format = Format::Rgba8;
    uint32_t samples = 1;
    Lifetime lifetime = Lifetime::Transient;
    bool initialized = false;
    uint32_t target = NO_TARGET;
};

struct Pass {
    uint32_t id = 0;
    Operation operation = Operation::ClearColor;
    uint32_t source_id = 0;
    uint32_t destination_id = 0;
};

struct Configuration {
    std::array<Resource, MAX_RESOURCES> resources{};
    std::array<Pass, MAX_PASSES> passes{};
    std::array<bool, MAX_RESOURCES> resource_set{};
    std::array<bool, MAX_PASSES> pass_set{};
    uint32_t resource_count = 0;
    uint32_t pass_count = 0;
    uint32_t output_id = 0;
    bool building = false;
    bool valid = false;
};

// Render-thread adapter for the validated Elisa planner. Configurations stage
// transactionally; a failed allocation leaves the current postprocess image
// selected. Physical transient slots come from the planner's proven lifetimes.
class Executor {
public:
    int32_t begin(uint32_t resources, uint32_t passes, uint32_t output_id) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (resources == 0 || resources > MAX_RESOURCES || passes == 0 ||
            passes > MAX_PASSES || output_id == 0) return INVALID_ARGUMENT;
        staging_ = {};
        staging_.resource_count = resources;
        staging_.pass_count = passes;
        staging_.output_id = output_id;
        staging_.building = true;
        return OK;
    }

    int32_t set_resource(uint32_t index, const Resource& resource) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!staging_.building || index >= staging_.resource_count ||
            resource.id == 0 || resource.samples != 1 ||
            (resource.size_mode != SizeMode::Fixed && resource.size_mode != SizeMode::PrimaryInternal) ||
            (resource.lifetime != Lifetime::Imported && resource.lifetime != Lifetime::Persistent &&
                resource.lifetime != Lifetime::Transient) ||
            !known_format(resource.format) || resource.format == Format::Depth32 ||
            (resource.size_mode == SizeMode::Fixed &&
                (resource.width == 0 || resource.height == 0 ||
                 resource.width > 16384 || resource.height > 16384)) ||
            (resource.size_mode == SizeMode::PrimaryInternal &&
                (resource.width != 0 || resource.height != 0)) ||
            (resource.lifetime == Lifetime::Imported &&
                (!resource.initialized || resource.target != NO_TARGET)) ||
            (resource.lifetime != Lifetime::Imported &&
                (resource.initialized && resource.lifetime == Lifetime::Transient)) ||
            (resource.lifetime != Lifetime::Imported && resource.target >= MAX_TARGETS)) {
            return INVALID_ARGUMENT;
        }
        for (uint32_t previous = 0; previous < staging_.resource_count; ++previous) {
            if (staging_.resource_set[previous] && previous != index &&
                staging_.resources[previous].id == resource.id) return INVALID_ARGUMENT;
        }
        staging_.resources[index] = resource;
        staging_.resource_set[index] = true;
        return OK;
    }

    int32_t set_pass(uint32_t index, const Pass& pass) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!staging_.building || index >= staging_.pass_count || pass.id == 0 ||
            (pass.operation != Operation::ClearColor && pass.operation != Operation::CopyColor) ||
            pass.destination_id == 0 ||
            (pass.operation == Operation::ClearColor && pass.source_id != 0) ||
            (pass.operation == Operation::CopyColor && pass.source_id == 0)) {
            return INVALID_ARGUMENT;
        }
        for (uint32_t previous = 0; previous < staging_.pass_count; ++previous) {
            if (staging_.pass_set[previous] && previous != index &&
                staging_.passes[previous].id == pass.id) return INVALID_ARGUMENT;
        }
        staging_.passes[index] = pass;
        staging_.pass_set[index] = true;
        return OK;
    }

    int32_t commit() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!staging_.building) return INVALID_ARGUMENT;
        for (uint32_t index = 0; index < staging_.resource_count; ++index) {
            if (!staging_.resource_set[index]) return INVALID_ARGUMENT;
        }
        for (uint32_t index = 0; index < staging_.pass_count; ++index) {
            if (!staging_.pass_set[index]) return INVALID_ARGUMENT;
        }
        if (!configuration_valid(staging_)) return INVALID_ARGUMENT;
        staging_.building = false;
        staging_.valid = true;
        active_ = staging_;
        staging_ = {};
        targets_dirty_ = true;
        last_status_ = OK;
        return OK;
    }

    void abort() {
        std::lock_guard<std::mutex> guard(mutex_);
        staging_ = {};
    }

    void reset() {
        std::lock_guard<std::mutex> guard(mutex_);
        staging_ = {};
        active_ = {};
        targets_ = {};
        internal_width_ = 0;
        internal_height_ = 0;
        targets_dirty_ = false;
        executions_ = 0;
        last_status_ = OK;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        fail_next_allocation_ = false;
        fail_pass_index_ = NO_TARGET;
        last_fallback_preserved_ = false;
#endif
    }

#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    int32_t fail_next_allocation_for_test() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid) return INVALID_ARGUMENT;
        fail_next_allocation_ = true;
        return OK;
    }

    int32_t fail_pass_for_test(uint32_t index) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid || index >= active_.pass_count) return INVALID_ARGUMENT;
        fail_pass_index_ = index;
        return OK;
    }

    bool last_fallback_preserved_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return last_fallback_preserved_;
    }
#endif

    void execute(const wi::RenderPath3D& path, wi::graphics::CommandList command_list) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid) return;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        last_fallback_preserved_ = false;
#endif
        const wi::graphics::Texture* imported = path.GetLastPostprocessRT();
        if (imported == nullptr || !imported->IsValid()) {
            last_status_ = BACKEND_FAILED;
            return;
        }
        const XMUINT2 resolution = path.GetInternalResolution();
        if (resolution.x == 0 || resolution.y == 0) return;
        if (targets_dirty_ || internal_width_ != resolution.x || internal_height_ != resolution.y) {
            if (!prepare_targets(*imported, resolution.x, resolution.y)) {
                last_status_ = BACKEND_FAILED;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
#endif
                return;
            }
        }
        const uint32_t imported_index = imported_resource_index(active_);
        if (imported_index == NO_TARGET) {
            last_status_ = INVALID_ARGUMENT;
            return;
        }
        for (uint32_t index = 0; index < active_.pass_count; ++index) {
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
            if (fail_pass_index_ == index) {
                fail_pass_index_ = NO_TARGET;
                last_status_ = BACKEND_FAILED;
                last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
                return;
            }
#endif
            const Pass& pass = active_.passes[index];
            wi::graphics::Texture* destination = texture_for(pass.destination_id, imported_index, imported);
            if (destination == nullptr) {
                last_status_ = BACKEND_FAILED;
                return;
            }
            if (pass.operation == Operation::ClearColor) {
                wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
                if (device == nullptr) {
                    last_status_ = BACKEND_FAILED;
                    return;
                }
                device->RenderPassBegin(destination, command_list, true);
                device->RenderPassEnd(command_list);
            } else {
                wi::graphics::Texture* source = texture_for(pass.source_id, imported_index, imported);
                if (source == nullptr || source == destination ||
                    source->GetDesc().format != destination->GetDesc().format ||
                    source->GetDesc().width != destination->GetDesc().width ||
                    source->GetDesc().height != destination->GetDesc().height) {
                    last_status_ = INVALID_ARGUMENT;
                    return;
                }
                copy_color(*source, *destination, command_list);
            }
        }
        const uint32_t output_index = resource_index(active_, active_.output_id);
        wi::graphics::Texture* output = texture_for(active_.output_id, imported_index, imported);
        if (output == nullptr || output_index == NO_TARGET) {
            last_status_ = BACKEND_FAILED;
            return;
        }
        path.lastPostprocessRT = output;
        ++executions_;
        last_status_ = OK;
    }

    int32_t last_status() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return last_status_;
    }

    uint64_t execution_count() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return executions_;
    }

private:
    static constexpr int32_t OK = 0;
    static constexpr int32_t INVALID_ARGUMENT = -1;
    static constexpr int32_t BACKEND_FAILED = -8;

    static bool known_format(Format format) {
        return format == Format::Rgba8 || format == Format::Rgba16Float ||
            format == Format::Depth32 || format == Format::R11G11B10Float;
    }

    static wi::graphics::Format native_format(Format format) {
        switch (format) {
        case Format::Rgba8: return wi::graphics::Format::R8G8B8A8_UNORM;
        case Format::Rgba16Float: return wi::graphics::Format::R16G16B16A16_FLOAT;
        case Format::R11G11B10Float: return wi::graphics::Format::R11G11B10_FLOAT;
        default: return wi::graphics::Format::UNKNOWN;
        }
    }

    static uint32_t resource_index(const Configuration& config, uint32_t id) {
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            if (config.resources[index].id == id) return index;
        }
        return NO_TARGET;
    }

    static uint32_t imported_resource_index(const Configuration& config) {
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            if (config.resources[index].lifetime == Lifetime::Imported) return index;
        }
        return NO_TARGET;
    }

    static bool configuration_valid(const Configuration& config) {
        uint32_t imported_count = 0;
        std::array<bool, MAX_RESOURCES> initialized{};
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            const Resource& resource = config.resources[index];
            if (resource.lifetime == Lifetime::Imported) ++imported_count;
            initialized[index] = resource.initialized;
            if (resource.lifetime == Lifetime::Imported) continue;
            for (uint32_t previous = 0; previous < index; ++previous) {
                const Resource& other = config.resources[previous];
                if (other.lifetime == Lifetime::Imported || other.target != resource.target) continue;
                if (other.size_mode != resource.size_mode || other.width != resource.width ||
                    other.height != resource.height || other.format != resource.format ||
                    other.samples != resource.samples) return false;
            }
        }
        if (imported_count != 1) return false;
        const uint32_t output_index = resource_index(config, config.output_id);
        if (output_index == NO_TARGET) return false;
        for (uint32_t index = 0; index < config.pass_count; ++index) {
            const Pass& pass = config.passes[index];
            const uint32_t destination = resource_index(config, pass.destination_id);
            if (destination == NO_TARGET || config.resources[destination].lifetime == Lifetime::Imported) return false;
            if (pass.operation == Operation::CopyColor) {
                const uint32_t source = resource_index(config, pass.source_id);
                if (source == NO_TARGET || source == destination || !initialized[source]) return false;
                const Resource& source_desc = config.resources[source];
                const Resource& destination_desc = config.resources[destination];
                if (source_desc.size_mode != destination_desc.size_mode ||
                    source_desc.width != destination_desc.width || source_desc.height != destination_desc.height ||
                    source_desc.format != destination_desc.format || source_desc.samples != destination_desc.samples) return false;
            }
            initialized[destination] = true;
        }
        return initialized[output_index];
    }

    bool prepare_targets(const wi::graphics::Texture& imported, uint32_t width, uint32_t height) {
        const wi::graphics::TextureDesc& imported_desc = imported.GetDesc();
        const uint32_t imported_index = [&]() {
            for (uint32_t index = 0; index < active_.resource_count; ++index) {
                if (active_.resources[index].lifetime == Lifetime::Imported) return index;
            }
            return NO_TARGET;
        }();
        if (imported_index == NO_TARGET) return false;
        const Resource& imported_resource = active_.resources[imported_index];
        const uint32_t expected_width = imported_resource.size_mode == SizeMode::PrimaryInternal ? width : imported_resource.width;
        const uint32_t expected_height = imported_resource.size_mode == SizeMode::PrimaryInternal ? height : imported_resource.height;
        if (imported_desc.width != expected_width || imported_desc.height != expected_height ||
            imported_desc.format != native_format(imported_resource.format)) return false;

        std::array<wi::graphics::Texture, MAX_TARGETS> candidate{};
        wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
        if (device == nullptr) return false;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        if (fail_next_allocation_) {
            fail_next_allocation_ = false;
            return false;
        }
#endif
        for (uint32_t index = 0; index < active_.resource_count; ++index) {
            const Resource& resource = active_.resources[index];
            if (resource.lifetime == Lifetime::Imported || candidate[resource.target].IsValid()) continue;
            wi::graphics::TextureDesc desc;
            desc.format = native_format(resource.format);
            desc.bind_flags = wi::graphics::BindFlag::RENDER_TARGET | wi::graphics::BindFlag::SHADER_RESOURCE;
            desc.width = resource.size_mode == SizeMode::PrimaryInternal ? width : resource.width;
            desc.height = resource.size_mode == SizeMode::PrimaryInternal ? height : resource.height;
            desc.sample_count = 1;
            desc.layout = wi::graphics::ResourceState::SHADER_RESOURCE;
            desc.clear.color[0] = 0.0f;
            desc.clear.color[1] = 0.0f;
            desc.clear.color[2] = 0.0f;
            desc.clear.color[3] = 0.0f;
            wi::graphics::Texture& texture = candidate[resource.target];
            const bool created = resource.lifetime == Lifetime::Persistent && resource.initialized
                ? device->CreateTextureZeroed(&desc, &texture)
                : device->CreateTexture(&desc, nullptr, &texture);
            if (!created || !texture.IsValid()) return false;
        }
        targets_.swap(candidate);
        internal_width_ = width;
        internal_height_ = height;
        targets_dirty_ = false;
        return true;
    }

    wi::graphics::Texture* texture_for(uint32_t id, uint32_t imported_index,
        const wi::graphics::Texture* imported) {
        const uint32_t index = resource_index(active_, id);
        if (index == NO_TARGET) return nullptr;
        const Resource& resource = active_.resources[index];
        if (index == imported_index) return const_cast<wi::graphics::Texture*>(imported);
        if (resource.target >= MAX_TARGETS) return nullptr;
        wi::graphics::Texture* texture = &targets_[resource.target];
        return texture->IsValid() ? texture : nullptr;
    }

    static void copy_color(wi::graphics::Texture& source, wi::graphics::Texture& destination,
        wi::graphics::CommandList command_list) {
        wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
        const wi::graphics::ResourceState source_layout = source.GetDesc().layout;
        const wi::graphics::ResourceState destination_layout = destination.GetDesc().layout;
        device->Barrier(wi::graphics::GPUBarrier::Image(&source, source_layout,
            wi::graphics::ResourceState::COPY_SRC), command_list);
        device->Barrier(wi::graphics::GPUBarrier::Image(&destination, destination_layout,
            wi::graphics::ResourceState::COPY_DST), command_list);
        device->CopyTexture(&destination, 0, 0, 0, 0, 0, &source, 0, 0, command_list);
        device->Barrier(wi::graphics::GPUBarrier::Image(&source,
            wi::graphics::ResourceState::COPY_SRC, source_layout), command_list);
        device->Barrier(wi::graphics::GPUBarrier::Image(&destination,
            wi::graphics::ResourceState::COPY_DST, destination_layout), command_list);
    }

    mutable std::mutex mutex_;
    Configuration staging_{};
    Configuration active_{};
    std::array<wi::graphics::Texture, MAX_TARGETS> targets_{};
    uint32_t internal_width_ = 0;
    uint32_t internal_height_ = 0;
    bool targets_dirty_ = false;
    int32_t last_status_ = OK;
    uint64_t executions_ = 0;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
    bool fail_next_allocation_ = false;
    uint32_t fail_pass_index_ = NO_TARGET;
    bool last_fallback_preserved_ = false;
#endif
};

} // namespace elisa::render_graph
