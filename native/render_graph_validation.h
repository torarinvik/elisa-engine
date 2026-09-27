#pragma once

#include "render_graph_types.h"

#include <array>

namespace elisa::render_graph {

inline uint32_t find_resource(const Configuration& config, uint32_t id) {
    for (uint32_t index = 0; index < config.resource_count; ++index) {
        if (config.resources[index].id == id) return index;
    }
    return NO_TARGET;
}

inline bool configuration_valid(const Configuration& config) {
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
    const uint32_t output_index = find_resource(config, config.output_id);
    if (output_index == NO_TARGET || config.resources[output_index].format == Format::Depth32) return false;
    for (uint32_t index = 0; index < config.pass_count; ++index) {
        const Pass& pass = config.passes[index];
        const uint32_t destination = find_resource(config, pass.destination_id);
        if (destination == NO_TARGET || config.resources[destination].lifetime == Lifetime::Imported) return false;
        if (pass.operation == Operation::ClearColor && config.resources[destination].format == Format::Depth32) return false;
        if (pass.operation == Operation::ClearDepth && config.resources[destination].format != Format::Depth32) return false;
        if (pass.operation == Operation::ClearColor || pass.operation == Operation::ClearDepth) {
            initialized[destination] = true;
            continue;
        }
        const uint32_t source = find_resource(config, pass.source_id);
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
        if (pass.operation == Operation::BlendColor) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float || destination_desc.format == Format::R11G11B10Float;
            if (!source_color || !destination_color || source_desc.samples != 1 ||
                destination_desc.samples != 1 || !initialized[destination]) return false;
            initialized[destination] = true;
            continue;
        }
        if (pass.operation == Operation::AdjustSaturation) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float || destination_desc.format == Format::R11G11B10Float;
            if (!source_color || !destination_color || source_desc.format != destination_desc.format ||
                source_desc.samples != 1 || destination_desc.samples != 1) return false;
            initialized[destination] = true;
            continue;
        }
        if (pass.operation == Operation::ScaleColor) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float || destination_desc.format == Format::R11G11B10Float;
            if (!source_color || !destination_color || source_desc.format != destination_desc.format ||
                source_desc.samples != 1 || destination_desc.samples != 1) return false;
            initialized[destination] = true;
            continue;
        }
        if (pass.operation == Operation::TintColor) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float || destination_desc.format == Format::R11G11B10Float;
            if (!source_color || !destination_color || source_desc.format != destination_desc.format ||
                source_desc.samples != 1 || destination_desc.samples != 1) return false;
            initialized[destination] = true;
            continue;
        }
        if (pass.operation == Operation::Fxaa) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_uav_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float;
            if (!source_color || !destination_uav_color || source_desc.samples != 1 ||
                destination_desc.samples != 1) return false;
            initialized[destination] = true;
            continue;
        }
        if (pass.operation == Operation::Sharpen) {
            const bool source_color = source_desc.format == Format::Rgba8 ||
                source_desc.format == Format::Rgba16Float || source_desc.format == Format::R11G11B10Float;
            const bool destination_uav_color = destination_desc.format == Format::Rgba8 ||
                destination_desc.format == Format::Rgba16Float;
            if (!source_color || !destination_uav_color || source_desc.samples != 1 ||
                destination_desc.samples != 1) return false;
            initialized[destination] = true;
            continue;
        }
        if (source_desc.format == Format::Depth32 || destination_desc.format == Format::Depth32 ||
            source_desc.format != destination_desc.format) return false;
        if (pass.operation == Operation::CopyColor &&
            (source_desc.samples != 1 || destination_desc.samples != 1)) return false;
        if (pass.operation == Operation::ResolveColor &&
            (source_desc.samples <= 1 || destination_desc.samples != 1)) return false;
        initialized[destination] = true;
    }
    return initialized[output_index];
}

} // namespace elisa::render_graph
