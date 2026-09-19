#pragma once
// Native glTF import through cgltf, the dependency the plan names for model
// import. The header is fetched and hash-pinned by
// scripts/fetch_dependencies.py into dependencies/ (git-ignored), so
// third-party code stays out of the Elisa-owned source tree.
//
// This is the import stage of the asset pipeline: source bytes become
// validated normalized counts. Creating renderer meshes from that data is a
// later step, so nothing here touches the GPU.
#include <cstdio>
#include <string>

#ifndef CGLTF_IMPLEMENTATION
#define CGLTF_IMPLEMENTATION
#endif
#include "cgltf.h"

namespace probe {

struct AssetSummary {
    int triangles = 0;
    int positions = 0;
    int nodes = 0;
    int primitives = 0;
    int materials = 0;
    int texture_references = 0;
    int cameras = 0;
    int lights = 0;
    int skins = 0;
    int animations = 0;
    int animation_channels = 0;
    int morph_targets = 0;
    int unsupported_extensions = 0;
    bool ok = false;
};

inline AssetSummary import_gltf_triangles(const std::string& path) {
    AssetSummary summary;
    cgltf_options options = {};
    cgltf_data* data = nullptr;
    if (cgltf_parse_file(&options, path.c_str(), &data) != cgltf_result_success) {
        return summary;
    }
    const bool loaded = cgltf_load_buffers(&options, data, path.c_str()) == cgltf_result_success;
    if (loaded && cgltf_validate(data) == cgltf_result_success) {
        summary.nodes = static_cast<int>(data->nodes_count);
        summary.materials = static_cast<int>(data->materials_count);
        summary.cameras = static_cast<int>(data->cameras_count);
        summary.lights = static_cast<int>(data->lights_count);
        summary.skins = static_cast<int>(data->skins_count);
        summary.animations = static_cast<int>(data->animations_count);
        summary.unsupported_extensions = static_cast<int>(data->extensions_required_count);
        for (cgltf_size material_index = 0; material_index < data->materials_count; ++material_index) {
            const cgltf_material& material = data->materials[material_index];
            if (material.has_pbr_metallic_roughness) {
                const auto& pbr = material.pbr_metallic_roughness;
                if (pbr.metallic_factor < 0.0f || pbr.metallic_factor > 1.0f ||
                    pbr.roughness_factor < 0.0f || pbr.roughness_factor > 1.0f) summary.unsupported_extensions += 1;
                if (pbr.base_color_texture.texture != nullptr) ++summary.texture_references;
                if (pbr.metallic_roughness_texture.texture != nullptr) ++summary.texture_references;
            }
            if (material.normal_texture.texture != nullptr) ++summary.texture_references;
            if (material.occlusion_texture.texture != nullptr) ++summary.texture_references;
            if (material.emissive_texture.texture != nullptr) ++summary.texture_references;
        }
        if (data->nodes_count > 4096 || data->meshes_count > 4096 || data->animations_count > 256) {
            summary.unsupported_extensions += 1;
        }
        for (cgltf_size node_index = 0; node_index < data->nodes_count; ++node_index) {
            const cgltf_node& node = data->nodes[node_index];
            if (node.parent != nullptr && node.parent >= data->nodes &&
                node.parent < data->nodes + data->nodes_count) continue;
            if (node.parent != nullptr) summary.unsupported_extensions += 1;
        }
        for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
            const cgltf_mesh& mesh = data->meshes[mesh_index];
            for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
                const cgltf_primitive& primitive = mesh.primitives[primitive_index];
                summary.primitives += 1;
                summary.morph_targets += static_cast<int>(primitive.targets_count);
                if (primitive.material != nullptr && (data->materials == nullptr ||
                    primitive.material < data->materials || primitive.material >= data->materials + data->materials_count)) {
                    summary.unsupported_extensions += 1;
                }
                if (primitive.type != cgltf_primitive_type_triangles || primitive.has_draco_mesh_compression) {
                    summary.unsupported_extensions += 1;
                }
                if (primitive.indices != nullptr) {
                    summary.triangles += (int)(primitive.indices->count / 3);
                } else if (primitive.attributes_count > 0 && primitive.attributes[0].data != nullptr) {
                    summary.triangles += (int)(primitive.attributes[0].data->count / 3);
                }
                for (cgltf_size attribute_index = 0; attribute_index < primitive.attributes_count; ++attribute_index) {
                    if (primitive.attributes[attribute_index].type == cgltf_attribute_type_position &&
                        primitive.attributes[attribute_index].data != nullptr) {
                        summary.positions += (int)primitive.attributes[attribute_index].data->count;
                    }
                }
            }
        }
        for (cgltf_size animation_index = 0; animation_index < data->animations_count; ++animation_index) {
            const cgltf_animation& animation = data->animations[animation_index];
            summary.animation_channels += static_cast<int>(animation.channels_count);
            for (cgltf_size channel_index = 0; channel_index < animation.channels_count; ++channel_index) {
                const cgltf_animation_channel& channel = animation.channels[channel_index];
                if (channel.target_node == nullptr || channel.sampler == nullptr ||
                    channel.sampler->input == nullptr || channel.sampler->output == nullptr) {
                    summary.unsupported_extensions += 1;
                }
            }
        }
    }
    cgltf_free(data);
    summary.ok = summary.triangles > 0 && summary.nodes > 0 && summary.primitives > 0 &&
        summary.unsupported_extensions == 0;
    return summary;
}

} // namespace probe
