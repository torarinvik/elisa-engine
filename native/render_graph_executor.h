#pragma once

#include "wiGraphics.h"
#include "wiRenderPath3D.h"
#include "render_graph_blend.h"
#include "render_graph_scale_color.h"
#include "render_graph_saturation.h"
#include "render_graph_tint.h"
#include "render_graph_types.h"
#include "render_graph_visualize.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <mutex>

namespace elisa::render_graph {

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
                    resource.import_source != ImportSource::SceneDepth))) ||
            (resource.lifetime != Lifetime::Imported &&
                ((resource.initialized && resource.lifetime == Lifetime::Transient) ||
                 resource.import_source != ImportSource::None)) ||
            (resource.lifetime == Lifetime::Imported && resource.import_source == ImportSource::None) ||
            (resource.import_source == ImportSource::SceneColor &&
                resource.format != Format::Rgba8 && resource.format != Format::Rgba16Float &&
                resource.format != Format::R11G11B10Float) ||
            (resource.import_source == ImportSource::SceneDepth && resource.format != Format::R32Float) ||
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
                pass.operation != Operation::VisualizeDepth && pass.operation != Operation::BlendColor &&
                pass.operation != Operation::AdjustSaturation && pass.operation != Operation::ScaleColor &&
                pass.operation != Operation::TintColor && pass.operation != Operation::Fxaa &&
                pass.operation != Operation::Sharpen && pass.operation != Operation::ChromaticAberration &&
                pass.operation != Operation::NormalsFromDepth) ||
            pass.destination_id == 0 ||
            ((pass.operation == Operation::ClearColor || pass.operation == Operation::ClearDepth) &&
                pass.source_id != 0) ||
            ((pass.operation == Operation::CopyColor || pass.operation == Operation::ResolveColor ||
                pass.operation == Operation::VisualizeDepth || pass.operation == Operation::BlendColor ||
                pass.operation == Operation::AdjustSaturation || pass.operation == Operation::ScaleColor ||
                pass.operation == Operation::TintColor || pass.operation == Operation::Fxaa ||
                pass.operation == Operation::Sharpen || pass.operation == Operation::ChromaticAberration ||
                pass.operation == Operation::NormalsFromDepth) &&
                pass.source_id == 0)) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.opacity) || pass.opacity < MIN_BLEND_OPACITY ||
            pass.opacity > MAX_BLEND_OPACITY ||
            (pass.operation != Operation::BlendColor && pass.opacity != DEFAULT_BLEND_OPACITY)) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.saturation) || pass.saturation < MIN_SATURATION ||
            pass.saturation > MAX_SATURATION ||
            (pass.operation != Operation::AdjustSaturation && pass.saturation != DEFAULT_SATURATION)) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.color_scale) || pass.color_scale < MIN_COLOR_SCALE ||
            pass.color_scale > MAX_COLOR_SCALE ||
            (pass.operation != Operation::ScaleColor && pass.color_scale != DEFAULT_COLOR_SCALE)) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.tint_red) || pass.tint_red < MIN_TINT_CHANNEL || pass.tint_red > MAX_TINT_CHANNEL ||
            !std::isfinite(pass.tint_green) || pass.tint_green < MIN_TINT_CHANNEL || pass.tint_green > MAX_TINT_CHANNEL ||
            !std::isfinite(pass.tint_blue) || pass.tint_blue < MIN_TINT_CHANNEL || pass.tint_blue > MAX_TINT_CHANNEL ||
            !std::isfinite(pass.tint_alpha) || pass.tint_alpha < MIN_TINT_ALPHA || pass.tint_alpha > MAX_TINT_ALPHA ||
            (pass.operation != Operation::TintColor &&
                (pass.tint_red != DEFAULT_TINT_CHANNEL || pass.tint_green != DEFAULT_TINT_CHANNEL ||
                 pass.tint_blue != DEFAULT_TINT_CHANNEL || pass.tint_alpha != DEFAULT_TINT_ALPHA))) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.sharpen_amount) || pass.sharpen_amount < MIN_SHARPEN_AMOUNT ||
            pass.sharpen_amount > MAX_SHARPEN_AMOUNT ||
            (pass.operation != Operation::Sharpen && pass.sharpen_amount != DEFAULT_SHARPEN_AMOUNT)) {
            return INVALID_ARGUMENT;
        }
        if (!std::isfinite(pass.chromatic_aberration) ||
            pass.chromatic_aberration < MIN_CHROMATIC_ABERRATION ||
            pass.chromatic_aberration > MAX_CHROMATIC_ABERRATION ||
            (pass.operation != Operation::ChromaticAberration &&
                pass.chromatic_aberration != DEFAULT_CHROMATIC_ABERRATION)) {
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
        scene_depth_read_count_ = 0;
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

    uint64_t scene_depth_read_count_for_test() const {
        std::lock_guard<std::mutex> guard(mutex_);
        return scene_depth_read_count_;
    }
#endif

    #include "render_graph_executor_execute.inc"

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
            if (!depth && target_requires_uav(resource.target)) {
                desc.bind_flags |= wi::graphics::BindFlag::UNORDERED_ACCESS;
            }
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
        if (resource.import_source == ImportSource::SceneDepth) return &path.depthBuffer_Copy;
        return nullptr;
    }

    bool target_requires_uav(uint32_t target) const {
        for (uint32_t index = 0; index < active_.pass_count; ++index) {
            const Pass& pass = active_.passes[index];
            if (pass.operation != Operation::Fxaa && pass.operation != Operation::Sharpen &&
                pass.operation != Operation::ChromaticAberration &&
                pass.operation != Operation::NormalsFromDepth) continue;
            const uint32_t destination = resource_index(active_, pass.destination_id);
            if (destination != NO_TARGET && active_.resources[destination].target == target) return true;
        }
        return false;
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
    uint64_t scene_depth_read_count_ = 0;
#endif
};

} // namespace elisa::render_graph

#include "render_graph_validation.h"
