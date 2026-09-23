#pragma once

// Plain normalized FBX import data shared by the engine's offline import stages.
#include <cstdint>
#include <array>
#include <limits>
#include <string>
#include <vector>

namespace elisa::assets {

struct FbxVertex {
    float position[3]{};
    float normal[3]{};
    float uv[2]{};
};
static_assert(sizeof(FbxVertex) == sizeof(float) * 8);

struct FbxSkinInfluence {
    uint32_t bone_indices[4]{};
    float bone_weights[4]{};
};
static_assert(sizeof(FbxSkinInfluence) == sizeof(float) * 4 + sizeof(uint32_t) * 4);

struct FbxSkinJoint {
    std::string name;
    int32_t parent_index = -1;
    // Translation, quaternion xyzw, and scale, in the importer-normalized
    // metre-space hierarchy. Ancestor helpers are included when needed.
    float rest_local[10]{};
    int32_t cluster_index = -1;
};

struct FbxAnimationClip {
    std::string name;
    float duration_seconds = 0.0f;
    uint32_t sample_rate = 0;
    uint32_t frame_count = 0;
    // Frame-major; each joint has translation xyz, quaternion xyzw, scale xyz.
    std::vector<float> local_transforms;
};

struct FbxMeshSubset {
    uint32_t index_start = 0;
    uint32_t index_count = 0;
    uint32_t material_slot = 0;
};

struct FbxMaterialData {
    std::string name;
    std::array<float, 4> base_color{1.0f, 1.0f, 1.0f, 1.0f};
    float metallic = 0.0f;
    float roughness = 1.0f;
    std::array<float, 3> emissive{};
    float alpha_cutoff = 0.5f;
    uint32_t alpha_mode = 0;
    bool double_sided = false;
    bool occlusion = false;
    // External image paths in the FBX source, ordered as base, normal,
    // packed surface, emissive and occlusion. Separate roughness and metalness
    // source maps are kept here until the cooker packs them into surface RGBA.
    std::array<std::string, 5> texture_sources{};
    std::array<std::string, 2> surface_texture_sources{};
};

struct FbxMeshPlacementData {
    uint32_t mesh = 0;
    uint32_t node = 0;
    uint32_t vertex_start = 0;
    uint32_t vertex_count = 0;
    uint32_t index_start = 0;
    uint32_t index_count = 0;
    uint32_t subset_start = 0;
    uint32_t subset_count = 0;
    // Row-major affine 3x4 source geometry-to-world transform. FBX positions
    // are baked into the cooked stream; this record preserves source identity.
    std::array<float, 12> transform{};
};

struct FbxMeshData {
    std::string node_name;
    std::string mesh_name;
    uint32_t source_mesh_count = 1;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    std::vector<FbxMeshSubset> subsets;
    std::vector<FbxMeshPlacementData> mesh_placements;
    std::vector<FbxMaterialData> materials;
    uint32_t material_slots = 1;
    std::vector<std::string> skin_bone_names;
    std::vector<uint32_t> skin_indices;
    std::vector<float> skin_weights;
    std::vector<FbxSkinJoint> skin_joints;
    std::vector<uint32_t> skin_cluster_joints;
    std::vector<FbxAnimationClip> animation_clips;
    float bounds_min[3] = {
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
        std::numeric_limits<float>::infinity(),
    };
    float bounds_max[3] = {
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(),
    };
};

struct FbxImportResult {
    std::string error;
    uint64_t file_bytes = 0;
    uint64_t nodes = 0;
    uint64_t meshes = 0;
    uint64_t vertices = 0;
    uint64_t triangles = 0;
    uint64_t bones = 0;
    uint64_t bone_name_hash = 14695981039346656037ull;
    uint64_t skins = 0;
    uint64_t materials = 0;
    uint64_t texture_references = 0;
    uint64_t animation_stacks = 0;
    double source_unit_meters = 0.0;
    double frames_per_second = 0.0;
    double first_clip_duration_seconds = 0.0;
    std::string first_clip_name;
    FbxMeshData primary_mesh;
    bool primary_mesh_extracted = false;
    bool ok = false;
};

} // namespace elisa::assets
