#pragma once

#include "wiGraphics.h"
#include "wiRenderPath3D.h"
#include "render_graph_visualize.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>

namespace elisa::render_graph {

constexpr uint32_t MAX_RESOURCES = 64;
constexpr uint32_t MAX_PASSES = 32;
constexpr uint32_t MAX_TARGETS = 64;
constexpr uint32_t NO_TARGET = std::numeric_limits<uint32_t>::max();

enum class SizeMode : int32_t { Fixed = 0, PrimaryInternal = 1 };
enum class Format : int32_t { Rgba8 = 0, Rgba16Float = 1, Depth32 = 2, R11G11B10Float = 3, R32Float = 4 };
enum class Lifetime : int32_t { Imported = 0, Persistent = 1, Transient = 2 };
enum class ImportSource : int32_t { None = 0, SceneColor = 1, LinearDepth = 2 };
enum class Operation : int32_t {
    ClearColor = 0, CopyColor = 1, ClearDepth = 2, ResolveColor = 3, VisualizeLinearDepth = 4
};

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
    ImportSource import_source = ImportSource::None;
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
            resource.id == 0 || !known_sample_count(resource.samples) ||
            (resource.size_mode != SizeMode::Fixed && resource.size_mode != SizeMode::PrimaryInternal) ||
            (resource.lifetime != Lifetime::Imported && resource.lifetime != Lifetime::Persistent &&
                resource.lifetime != Lifetime::Transient) ||
            !known_format(resource.format) ||
            (resource.size_mode == SizeMode::Fixed &&
                (resource.width == 0 || resource.height == 0 ||
                 resource.width > 16384 || resource.height > 16384)) ||
            (resource.size_mode == SizeMode::PrimaryInternal &&
                (resource.width != 0 || resource.height != 0)) ||
            (resource.lifetime == Lifetime::Imported &&
                (!resource.initialized || resource.target != NO_TARGET || resource.samples != 1 ||
                 (resource.import_source != ImportSource::SceneColor &&
                    resource.import_source != ImportSource::LinearDepth))) ||
            (resource.lifetime != Lifetime::Imported &&
                ((resource.initialized && resource.lifetime == Lifetime::Transient) ||
                 resource.import_source != ImportSource::None)) ||
            (resource.lifetime == Lifetime::Imported && resource.import_source == ImportSource::None) ||
            (resource.import_source == ImportSource::SceneColor &&
                resource.format != Format::Rgba8 && resource.format != Format::Rgba16Float &&
                resource.format != Format::R11G11B10Float) ||
            (resource.import_source == ImportSource::LinearDepth && resource.format != Format::R32Float) ||
            (resource.format == Format::Depth32 && resource.lifetime == Lifetime::Persistent &&
                resource.initialized) ||
            (resource.format == Format::Depth32 && resource.samples != 1) ||
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
            (pass.operation != Operation::ClearColor && pass.operation != Operation::CopyColor &&
                pass.operation != Operation::ClearDepth && pass.operation != Operation::ResolveColor &&
                pass.operation != Operation::VisualizeLinearDepth) ||
            pass.destination_id == 0 ||
            ((pass.operation == Operation::ClearColor || pass.operation == Operation::ClearDepth) &&
                pass.source_id != 0) ||
            ((pass.operation == Operation::CopyColor || pass.operation == Operation::ResolveColor ||
                pass.operation == Operation::VisualizeLinearDepth) &&
                pass.source_id == 0)) {
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
        depth_clear_count_ = 0;
        color_resolve_count_ = 0;
        linear_depth_read_count_ = 0;
        fail_next_allocation_ = false;
        fail_pass_index_ = NO_TARGET;
        suspend_next_frame_ = false;
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

    int32_t suspend_next_frame_for_test() {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid) return INVALID_ARGUMENT;
        suspend_next_frame_ = true;
        return OK;
    }

    bool last_fallback_preserved_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return last_fallback_preserved_;
    }

    uint64_t depth_clear_count_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return depth_clear_count_;
    }

    uint64_t color_resolve_count_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return color_resolve_count_;
    }

    uint64_t linear_depth_read_count_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return linear_depth_read_count_;
    }
#endif

    void execute(const wi::RenderPath3D& path, wi::graphics::CommandList command_list) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (!active_.valid) return;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        last_fallback_preserved_ = false;
#endif
        const wi::graphics::Texture* imported = path.GetLastPostprocessRT();
        if (imported == nullptr || !imported->IsValid() || scene_color_import_index(active_) == NO_TARGET) {
            last_status_ = BACKEND_FAILED;
            return;
        }
        const XMUINT2 resolution = path.GetInternalResolution();
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
        const bool suspend = suspend_next_frame_;
        suspend_next_frame_ = false;
#else
        constexpr bool suspend = false;
#endif
        if (suspend || resolution.x == 0 || resolution.y == 0) {
            last_status_ = SUSPENDED;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
            last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
#endif
            return;
        }
        if (targets_dirty_ || internal_width_ != resolution.x || internal_height_ != resolution.y) {
            if (!prepare_targets(path, resolution.x, resolution.y)) {
                last_status_ = BACKEND_FAILED;
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                last_fallback_preserved_ = path.GetLastPostprocessRT() == imported;
#endif
                return;
            }
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
            wi::graphics::Texture* destination = texture_for(pass.destination_id, path);
            if (destination == nullptr) {
                last_status_ = BACKEND_FAILED;
                return;
            }
            if (pass.operation == Operation::ClearColor) {
                wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
                if (device == nullptr || destination->GetDesc().format == native_format(Format::Depth32)) {
                    last_status_ = BACKEND_FAILED;
                    return;
                }
                device->RenderPassBegin(destination, command_list, true);
                device->RenderPassEnd(command_list);
            } else if (pass.operation == Operation::ClearDepth) {
                wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
                if (device == nullptr || destination->GetDesc().format != native_format(Format::Depth32)) {
                    last_status_ = BACKEND_FAILED;
                    return;
                }
                const wi::graphics::RenderPassImage depth =
                    wi::graphics::RenderPassImage::DepthStencil(destination,
                        wi::graphics::RenderPassImage::LoadOp::CLEAR);
                device->RenderPassBegin(&depth, 1, command_list);
                device->RenderPassEnd(command_list);
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                ++depth_clear_count_;
#endif
            } else {
                wi::graphics::Texture* source = texture_for(pass.source_id, path);
                if (source == nullptr || source == destination ||
                    source->GetDesc().width != destination->GetDesc().width ||
                    source->GetDesc().height != destination->GetDesc().height) {
                    last_status_ = INVALID_ARGUMENT;
                    return;
                }
                if (pass.operation == Operation::VisualizeLinearDepth) {
                    if (source->GetDesc().format != native_format(Format::R32Float) ||
                        destination->GetDesc().format == native_format(Format::Depth32) ||
                        source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!visualize_linear_depth(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                    ++linear_depth_read_count_;
#endif
                } else if (source->GetDesc().format == native_format(Format::Depth32) ||
                    destination->GetDesc().format == native_format(Format::Depth32) ||
                    source->GetDesc().format != destination->GetDesc().format) {
                    last_status_ = INVALID_ARGUMENT;
                    return;
                } else if (pass.operation == Operation::CopyColor) {
                    if (source->GetDesc().sample_count != 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!copy_color(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
                } else {
                    if (source->GetDesc().sample_count <= 1 || destination->GetDesc().sample_count != 1) {
                        last_status_ = INVALID_ARGUMENT;
                        return;
                    }
                    if (!resolve_color(*source, *destination, command_list)) {
                        last_status_ = BACKEND_FAILED;
                        return;
                    }
#if defined(ELISA_RENDER_SCENE_TEST_PROBE)
                    ++color_resolve_count_;
#endif
                }
            }
        }
        const uint32_t output_index = resource_index(active_, active_.output_id);
        wi::graphics::Texture* output = texture_for(active_.output_id, path);
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
    static constexpr int32_t SUSPENDED = 1;
    static constexpr int32_t INVALID_ARGUMENT = -1;
    static constexpr int32_t BACKEND_FAILED = -8;

    static bool known_format(Format format) {
        return format == Format::Rgba8 || format == Format::Rgba16Float ||
            format == Format::Depth32 || format == Format::R11G11B10Float ||
            format == Format::R32Float;
    }

    static bool known_sample_count(uint32_t samples) {
        return samples == 1 || samples == 2 || samples == 4 || samples == 8;
    }

    static wi::graphics::Format native_format(Format format) {
        switch (format) {
        case Format::Rgba8: return wi::graphics::Format::R8G8B8A8_UNORM;
        case Format::Rgba16Float: return wi::graphics::Format::R16G16B16A16_FLOAT;
        case Format::Depth32: return wi::graphics::Format::D32_FLOAT;
        case Format::R11G11B10Float: return wi::graphics::Format::R11G11B10_FLOAT;
        case Format::R32Float: return wi::graphics::Format::R32_FLOAT;
        default: return wi::graphics::Format::UNKNOWN;
        }
    }

    static uint32_t resource_index(const Configuration& config, uint32_t id) {
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            if (config.resources[index].id == id) return index;
        }
        return NO_TARGET;
    }

    static uint32_t scene_color_import_index(const Configuration& config) {
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            if (config.resources[index].import_source == ImportSource::SceneColor) return index;
        }
        return NO_TARGET;
    }

    static bool configuration_valid(const Configuration& config) {
        uint32_t scene_color_import_count = 0;
        uint32_t linear_depth_import_count = 0;
        std::array<bool, MAX_RESOURCES> initialized{};
        for (uint32_t index = 0; index < config.resource_count; ++index) {
            const Resource& resource = config.resources[index];
            if (resource.import_source == ImportSource::SceneColor) ++scene_color_import_count;
            if (resource.import_source == ImportSource::LinearDepth) ++linear_depth_import_count;
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
        if (scene_color_import_count != 1 || linear_depth_import_count > 1) return false;
        const uint32_t output_index = resource_index(config, config.output_id);
        if (output_index == NO_TARGET || config.resources[output_index].format == Format::Depth32) return false;
        for (uint32_t index = 0; index < config.pass_count; ++index) {
            const Pass& pass = config.passes[index];
            const uint32_t destination = resource_index(config, pass.destination_id);
            if (destination == NO_TARGET || config.resources[destination].lifetime == Lifetime::Imported) return false;
            if (pass.operation == Operation::ClearColor && config.resources[destination].format == Format::Depth32) return false;
            if (pass.operation == Operation::ClearDepth && config.resources[destination].format != Format::Depth32) return false;
            if (pass.operation == Operation::CopyColor || pass.operation == Operation::ResolveColor ||
                pass.operation == Operation::VisualizeLinearDepth) {
                const uint32_t source = resource_index(config, pass.source_id);
                if (source == NO_TARGET || source == destination || !initialized[source]) return false;
                const Resource& source_desc = config.resources[source];
                const Resource& destination_desc = config.resources[destination];
                if (source_desc.size_mode != destination_desc.size_mode ||
                    source_desc.width != destination_desc.width || source_desc.height != destination_desc.height) return false;
                if (pass.operation == Operation::VisualizeLinearDepth) {
                    if (source_desc.format != Format::R32Float || destination_desc.format == Format::Depth32 ||
                        source_desc.samples != 1 || destination_desc.samples != 1) return false;
                    initialized[destination] = true;
                    continue;
                }
                if (source_desc.format == Format::Depth32 || destination_desc.format == Format::Depth32 ||
                    source_desc.format != destination_desc.format) return false;
                if (pass.operation == Operation::CopyColor &&
                    (source_desc.samples != 1 || destination_desc.samples != 1)) return false;
                if (pass.operation == Operation::ResolveColor &&
                    (source_desc.samples <= 1 || destination_desc.samples != 1)) return false;
            }
            initialized[destination] = true;
        }
        return initialized[output_index];
    }

    bool prepare_targets(const wi::RenderPath3D& path, uint32_t width, uint32_t height) {
        for (uint32_t index = 0; index < active_.resource_count; ++index) {
            const Resource& resource = active_.resources[index];
            if (resource.lifetime != Lifetime::Imported) continue;
            const wi::graphics::Texture* imported = import_texture(resource, path);
            if (imported == nullptr || !imported->IsValid()) return false;
            const wi::graphics::TextureDesc& imported_desc = imported->GetDesc();
            const uint32_t expected_width = resource.size_mode == SizeMode::PrimaryInternal ? width : resource.width;
            const uint32_t expected_height = resource.size_mode == SizeMode::PrimaryInternal ? height : resource.height;
            if (imported_desc.width != expected_width || imported_desc.height != expected_height ||
                imported_desc.format != native_format(resource.format) || imported_desc.sample_count != 1) return false;
        }

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
            const bool depth = resource.format == Format::Depth32;
            desc.bind_flags = depth
                ? wi::graphics::BindFlag::DEPTH_STENCIL | wi::graphics::BindFlag::SHADER_RESOURCE
                : wi::graphics::BindFlag::RENDER_TARGET | wi::graphics::BindFlag::SHADER_RESOURCE;
            desc.width = resource.size_mode == SizeMode::PrimaryInternal ? width : resource.width;
            desc.height = resource.size_mode == SizeMode::PrimaryInternal ? height : resource.height;
            desc.sample_count = resource.samples;
            desc.layout = depth ? wi::graphics::ResourceState::DEPTHSTENCIL : wi::graphics::ResourceState::SHADER_RESOURCE;
            if (depth) {
                desc.clear.depth_stencil.depth = 1.0f;
                desc.clear.depth_stencil.stencil = 0;
            } else {
                desc.clear.color[0] = 0.0f;
                desc.clear.color[1] = 0.0f;
                desc.clear.color[2] = 0.0f;
                desc.clear.color[3] = 0.0f;
            }
            wi::graphics::Texture& texture = candidate[resource.target];
            const bool created = resource.lifetime == Lifetime::Persistent && resource.initialized
                ? device->CreateTextureZeroed(&desc, &texture)
                : device->CreateTexture(&desc, nullptr, &texture);
            if (!created || !texture.IsValid() || texture.GetDesc().sample_count != resource.samples) return false;
        }
        targets_.swap(candidate);
        internal_width_ = width;
        internal_height_ = height;
        targets_dirty_ = false;
        return true;
    }

    static const wi::graphics::Texture* import_texture(const Resource& resource,
        const wi::RenderPath3D& path) {
        if (resource.import_source == ImportSource::SceneColor) return path.GetLastPostprocessRT();
        if (resource.import_source == ImportSource::LinearDepth) return &path.depthBuffer_Copy;
        return nullptr;
    }

    wi::graphics::Texture* texture_for(uint32_t id, const wi::RenderPath3D& path) {
        const uint32_t index = resource_index(active_, id);
        if (index == NO_TARGET) return nullptr;
        const Resource& resource = active_.resources[index];
        if (resource.lifetime == Lifetime::Imported) {
            return const_cast<wi::graphics::Texture*>(import_texture(resource, path));
        }
        if (resource.target >= MAX_TARGETS) return nullptr;
        wi::graphics::Texture* texture = &targets_[resource.target];
        return texture->IsValid() ? texture : nullptr;
    }

    static bool copy_color(wi::graphics::Texture& source, wi::graphics::Texture& destination,
        wi::graphics::CommandList command_list) {
        wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
        if (device == nullptr) return false;
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
        return true;
    }

    static bool resolve_color(wi::graphics::Texture& source, wi::graphics::Texture& destination,
        wi::graphics::CommandList command_list) {
        wi::graphics::RenderPassImage images[] = {
            wi::graphics::RenderPassImage::RenderTarget(&source,
                wi::graphics::RenderPassImage::LoadOp::LOAD),
            wi::graphics::RenderPassImage::Resolve(&destination),
        };
        wi::graphics::GraphicsDevice* device = wi::graphics::GetDevice();
        if (device == nullptr) return false;
        device->RenderPassBegin(images, static_cast<uint32_t>(std::size(images)), command_list);
        device->RenderPassEnd(command_list);
        return true;
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
    bool suspend_next_frame_ = false;
    bool last_fallback_preserved_ = false;
    uint64_t depth_clear_count_ = 0;
    uint64_t color_resolve_count_ = 0;
    uint64_t linear_depth_read_count_ = 0;
#endif
};

} // namespace elisa::render_graph
