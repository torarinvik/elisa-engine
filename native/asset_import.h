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
        for (cgltf_size mesh_index = 0; mesh_index < data->meshes_count; ++mesh_index) {
            const cgltf_mesh& mesh = data->meshes[mesh_index];
            for (cgltf_size primitive_index = 0; primitive_index < mesh.primitives_count; ++primitive_index) {
                const cgltf_primitive& primitive = mesh.primitives[primitive_index];
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
    }
    cgltf_free(data);
    summary.ok = summary.triangles > 0;
    return summary;
}

} // namespace probe
