#pragma once

// Bounded reader for the engine's normalized triangle-mesh package. FBX is
// cooked offline; the runtime accepts only this versioned, validated format.
#include "bundle_dependencies.h"
#include "cooked_package_fields.h"
#include "cooked_geometry_limits.h"
#include "cooked_slot_materials.h"
#include "cooked_geometry_streams.h"
#include "cooked_geometry_uv1.h"
#include "virtual_package.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace elisa::assets {
inline constexpr uint32_t MAX_GEOMETRY_SUBSETS = 16;
inline constexpr uint32_t MAX_GEOMETRY_MATERIAL_SLOTS = 16;

struct CookedGeometry {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> uv1s;
    std::vector<float> tangents;
    std::string uv1_source;
    uint32_t uv1_generation_resolution = 0;
    uint32_t uv1_generation_padding = 0;
    uint32_t uv1_chart_count = 0;
    std::vector<uint32_t> indices;
    std::vector<std::string> skin_bone_names;
    std::vector<uint32_t> skin_indices;
    std::vector<float> skin_weights;
    // Optional authored glTF inverse-bind matrices in skin palette order,
    // stored as the source MAT4 column-major float stream.
    std::vector<float> skin_inverse_bind_matrices;
    struct SkinJoint {
        std::string name;
        int32_t parent_index = -1;
        std::array<float, 10> rest_local{};
        int32_t cluster_index = -1;
    };
    struct AnimationClip {
        std::string name;
        float duration_seconds = 0.0f;
        uint32_t sample_rate = 0;
        uint32_t frame_count = 0;
        std::vector<float> local_transforms;
        std::vector<float> morph_weights;
    };
    std::vector<SkinJoint> skin_joints;
    std::vector<uint32_t> skin_cluster_joints;
    std::vector<AnimationClip> animation_clips;
    struct MorphTarget {
        std::vector<float> positions;
        std::vector<float> normals;
    };
    std::vector<MorphTarget> morph_targets;
    std::vector<float> morph_default_weights;
    struct Camera {
        uint32_t node = 0;
        uint32_t projection = 0;
        float x = 0.0f;
        float y = 0.0f;
        float near_clip = 0.0f;
        float far_clip = 0.0f;
        std::array<float, 12> transform{};
    };
    struct Light {
        uint32_t node = 0;
        uint32_t kind = 0;
        float intensity = 0.0f;
        float range = 0.0f;
        float inner_cone = 0.0f;
        float outer_cone = 0.0f;
        std::array<float, 3> color{};
        std::array<float, 12> transform{};
    };
    std::vector<Camera> cameras;
    std::vector<Light> lights;
    struct MeshPlacement {
        uint32_t mesh = 0;
        uint32_t node = 0;
        uint32_t vertex_start = 0;
        uint32_t vertex_count = 0;
        uint32_t index_start = 0;
        uint32_t index_count = 0;
        uint32_t subset_start = 0;
        uint32_t subset_count = 0;
        std::array<float, 12> transform{};
    };
    uint32_t mesh_count = 0;
    std::vector<MeshPlacement> mesh_placements;
    struct Subset {
        uint32_t index_start = 0;
        uint32_t index_count = 0;
        uint32_t material_slot = 0;
    };
    // An exact, ordered partition of `indices`. Each subset draws with the
    // material its slot resolves to; a package without subset records holds
    // one subset over every index in slot 0.
    std::vector<Subset> subsets;
    uint32_t material_slots = 1;
    using SlotMaterial = CookedSlotMaterial;
    // One entry per material slot, or none when the source authored no
    // material factors and the game supplies every slot's material.
    std::vector<SlotMaterial> slot_materials;
    // Source labels stay parallel to authored slot factors when supplied.
    std::vector<std::string> slot_material_names;
    // Image sections of the enclosing ELPK bundle that slot materials sample,
    // and each section's checksum when the mesh loaded.
    std::vector<std::string> texture_sections;
    std::vector<uint32_t> texture_checksums;
};

inline bool resolve_project_asset_path(const char* asset_path, std::filesystem::path& resolved) {
    if (asset_path == nullptr) return false;
    size_t length = 0;
    while (length <= 4096 && asset_path[length] != '\0') ++length;
    if (length == 0 || length > 4096) return false;
    const std::string value(asset_path, length);
    if (value.front() == '/' || value.find('\\') != std::string::npos ||
        value.find_first_of("\r\n") != std::string::npos) return false;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const std::string component = value.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }

    std::error_code filesystem_error;
    std::filesystem::path root;
    if (!project_asset_root(root)) return false;
    const std::filesystem::path candidate = std::filesystem::canonical(root / std::filesystem::u8path(value), filesystem_error);
    if (filesystem_error || !std::filesystem::is_regular_file(candidate, filesystem_error) || filesystem_error) return false;
    auto root_part = root.begin();
    auto candidate_part = candidate.begin();
    for (; root_part != root.end(); ++root_part, ++candidate_part) {
        if (candidate_part == candidate.end() || *root_part != *candidate_part) return false;
    }
    resolved = candidate;
    return true;
}

namespace detail {

inline bool valid_inverse_bind_matrix(const float* matrix) {
    for (size_t component = 0; component < 16; ++component) {
        if (!std::isfinite(matrix[component])) return false;
    }
    if (std::abs(matrix[3]) > 1.0e-5f || std::abs(matrix[7]) > 1.0e-5f ||
        std::abs(matrix[11]) > 1.0e-5f || std::abs(matrix[15] - 1.0f) > 1.0e-5f) return false;
    const double a = matrix[0], b = matrix[4], c = matrix[8];
    const double d = matrix[1], e = matrix[5], f = matrix[9];
    const double g = matrix[2], h = matrix[6], i = matrix[10];
    const double determinant = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    return std::isfinite(determinant) && determinant != 0.0;
}

inline bool parse_geometry_morphs(const probe::PackageIndex& package, CookedGeometry& geometry,
    uint64_t vertex_count, std::string& error);
inline bool parse_geometry_scene(const probe::PackageIndex& package, CookedGeometry& geometry,
    std::string& error);
inline bool parse_geometry_animations(const probe::PackageIndex& package, CookedGeometry& geometry,
    std::string& error);

// Subset records are all present or all absent. Static and skinned geometry
// use the same ordered index partition; only the armature remains per entity.
inline bool parse_geometry_subsets(const probe::PackageIndex& package, CookedGeometry& geometry,
    std::string& error) {
    size_t present = 0;
    for (const char* key : {"material_slots", "subset_count", "subset_stride", "subsets_b64"}) {
        present += package.sections.count(key);
    }
    const uint32_t index_count = uint32_t(geometry.indices.size());
    if (present == 0) {
        geometry.subsets = {{0, index_count, 0}};
        return true;
    }
    uint64_t slots = 0;
    uint64_t count = 0;
    std::vector<uint32_t> words;
    if (present != 4 || !parse_count(package, "material_slots", slots) || slots == 0 ||
        slots > MAX_GEOMETRY_MATERIAL_SLOTS || !parse_count(package, "subset_count", count) ||
        count == 0 || count > MAX_GEOMETRY_SUBSETS || package.sections.at("subset_stride") != "12" ||
        !decode_u32(package, "subsets_b64", size_t(count) * 3, words)) {
        error = "invalid cooked geometry subset records";
        return false;
    }
    uint32_t next = 0;
    for (size_t subset = 0; subset < size_t(count); ++subset) {
        const uint32_t start = words[subset * 3];
        const uint32_t indices = words[subset * 3 + 1];
        const uint32_t slot = words[subset * 3 + 2];
        if (start != next || indices == 0 || indices % 3 != 0 || indices > index_count - start) {
            error = "cooked geometry subsets do not partition the index stream";
            return false;
        }
        if (slot >= slots) {
            error = "cooked geometry subset names a missing material slot";
            return false;
        }
        next = start + indices;
        geometry.subsets.push_back({start, indices, slot});
    }
    if (next != index_count) {
        error = "cooked geometry subsets do not partition the index stream";
        return false;
    }
    geometry.material_slots = uint32_t(slots);
    return true;
}

} // namespace detail

inline bool load_cooked_geometry_bytes(const uint8_t* bytes, size_t byte_count,
    CookedGeometry& geometry, std::string& error) {
    if (bytes == nullptr || byte_count == 0 || byte_count > probe::PackageIndex::MAX_PACKAGE_BYTES) {
        error = "cooked geometry byte stream exceeds its bound";
        return false;
    }
    const std::string text(reinterpret_cast<const char*>(bytes), byte_count);
    const probe::PackageIndex package = probe::parse_package_index(text);
    if (!package.valid) {
        error = package.error.empty() ? "invalid cooked geometry package" : package.error;
        return false;
    }
    geometry = CookedGeometry{};
    const auto format = package.sections.find("format");
    if (format == package.sections.end() ||
        (format->second != "elisa-cooked-v2" && format->second != "elisa-cooked-v3")) {
        error = "unsupported cooked geometry format";
        return false;
    }
    const bool has_rig_format = format->second == "elisa-cooked-v3";
    const auto stride = [&package](const char* key, const char* expected) {
        const auto found = package.sections.find(key);
        return found != package.sections.end() && found->second == expected;
    };
    const auto meshopt_codec = package.sections.find("meshopt_codec");
    size_t compressed_stream_count = 0;
    for (const auto& section : package.sections) {
        constexpr char suffix[] = "_meshopt_b64";
        if (section.first.size() >= sizeof(suffix) - 1 &&
            section.first.compare(section.first.size() - (sizeof(suffix) - 1),
                sizeof(suffix) - 1, suffix) == 0) {
            if (section.first != "positions_meshopt_b64" && section.first != "normals_meshopt_b64" &&
                section.first != "uvs_meshopt_b64" && section.first != "indices_meshopt_b64") {
                error = "unsupported meshoptimizer cooked geometry stream";
                return false;
            }
            ++compressed_stream_count;
        }
    }
    if ((compressed_stream_count == 0) != (meshopt_codec == package.sections.end()) ||
        (meshopt_codec != package.sections.end() &&
            meshopt_codec->second != "meshoptimizer-v1.2")) {
        error = "unsupported or inconsistent cooked geometry meshoptimizer codec";
        return false;
    }
    if (!stride("position_stride", "12") || !stride("normal_stride", "12") ||
        !stride("uv_stride", "8") || !stride("index_stride", "4")) {
        error = "unsupported cooked geometry strides";
        return false;
    }
    uint64_t triangles = 0;
    uint64_t vertices = 0;
    uint64_t indices = 0;
    if (!detail::parse_count(package, "triangles", triangles) || triangles == 0 || triangles > 5'000'000 ||
        !detail::parse_count(package, "positions", vertices) || vertices == 0 || vertices > 2'000'000 ||
        !detail::parse_count(package, "indices", indices) || indices != triangles * 3 ||
        vertices > std::numeric_limits<size_t>::max() / 3 ||
        indices > std::numeric_limits<size_t>::max()) {
        error = "invalid or excessive cooked geometry counts";
        return false;
    }
    if (!detail::decode_geometry_floats(package, "positions", size_t(vertices), 12, geometry.positions) ||
        !detail::decode_geometry_floats(package, "normals", size_t(vertices), 12, geometry.normals) ||
        !detail::decode_geometry_floats(package, "uvs", size_t(vertices), 8, geometry.uvs)) {
        error = "invalid cooked geometry vertex streams";
        return false;
    }
    if (!detail::parse_geometry_uv1(package, vertices, geometry.uv1s, geometry.uv1_source,
            geometry.uv1_generation_resolution, geometry.uv1_generation_padding,
            geometry.uv1_chart_count, error)) {
        return false;
    }
    const auto tangent_stride = package.sections.find("tangent_stride");
    const auto encoded_tangents = package.sections.find("tangents_b64");
    if ((tangent_stride == package.sections.end()) != (encoded_tangents == package.sections.end())) {
        error = "incomplete cooked geometry tangent stream";
        return false;
    }
    if (tangent_stride != package.sections.end()) {
        if (tangent_stride->second != "16" ||
            !detail::decode_floats(package, "tangents_b64", size_t(vertices) * 4, geometry.tangents)) {
            error = "invalid cooked geometry tangent stream";
            return false;
        }
        for (size_t vertex = 0; vertex < size_t(vertices); ++vertex) {
            const double tx = geometry.tangents[vertex * 4];
            const double ty = geometry.tangents[vertex * 4 + 1];
            const double tz = geometry.tangents[vertex * 4 + 2];
            const double tw = geometry.tangents[vertex * 4 + 3];
            const double nx = geometry.normals[vertex * 3];
            const double ny = geometry.normals[vertex * 3 + 1];
            const double nz = geometry.normals[vertex * 3 + 2];
            const double tangent_length = std::sqrt(tx * tx + ty * ty + tz * tz);
            if (std::abs(tangent_length - 1.0) > 0.02 || std::abs(tx * nx + ty * ny + tz * nz) > 0.02 ||
                std::abs(std::abs(tw) - 1.0) > 1.0e-4) {
                error = "cooked geometry tangent frame is not normalized or orthogonal";
                return false;
            }
        }
    }
    std::vector<uint8_t> index_bytes;
    if (!detail::decode_geometry_index_bytes(package, size_t(indices), index_bytes)) {
        error = "invalid cooked geometry index stream";
        return false;
    }
    geometry.indices.resize(size_t(indices));
    for (size_t index = 0; index < geometry.indices.size(); ++index) {
        const size_t offset = index * 4;
        const uint32_t value = uint32_t(index_bytes[offset]) |
            (uint32_t(index_bytes[offset + 1]) << 8) |
            (uint32_t(index_bytes[offset + 2]) << 16) |
            (uint32_t(index_bytes[offset + 3]) << 24);
        if (value >= vertices) {
            error = "cooked geometry index is out of range";
            return false;
        }
        geometry.indices[index] = value;
    }

    const auto skin_bones = package.sections.find("skin_bones");
    const auto skin_index_stride = package.sections.find("skin_indices_stride");
    const auto skin_weight_stride = package.sections.find("skin_weights_stride");
    const auto skin_indices = package.sections.find("skin_indices_b64");
    const auto skin_weights = package.sections.find("skin_weights_b64");
    const auto skin_names = package.sections.find("skin_names_b64");
    const auto inverse_bind_stride = package.sections.find("skin_inverse_bind_stride");
    const auto inverse_bind_data = package.sections.find("skin_inverse_bind_matrices_b64");
    const bool has_skin = skin_bones != package.sections.end();
    const bool has_inverse_bind = inverse_bind_stride != package.sections.end();
    if ((skin_index_stride != package.sections.end()) != has_skin ||
        (skin_weight_stride != package.sections.end()) != has_skin ||
        (skin_indices != package.sections.end()) != has_skin ||
        (skin_weights != package.sections.end()) != has_skin ||
        (skin_names != package.sections.end()) != has_skin ||
        (inverse_bind_data != package.sections.end()) != has_inverse_bind ||
        (has_inverse_bind && !has_skin)) {
        error = "incomplete cooked geometry skin stream";
        return false;
    }
    if (has_skin) {
        uint64_t bone_count = 0;
        if (!detail::parse_count(package, "skin_bones", bone_count) || bone_count == 0 ||
            bone_count > MAX_GEOMETRY_SKIN_BONES ||
            skin_index_stride->second != "16" || skin_weight_stride->second != "16" ||
            vertices > std::numeric_limits<size_t>::max() / 4 ||
            !detail::decode_u32(package, "skin_indices_b64", size_t(vertices) * 4, geometry.skin_indices) ||
            !detail::decode_floats(package, "skin_weights_b64", size_t(vertices) * 4, geometry.skin_weights)) {
            error = "invalid cooked geometry skin counts or influence streams";
            return false;
        }
        std::vector<uint8_t> name_bytes;
        if (!detail::decode_base64(skin_names->second, name_bytes) ||
            !detail::decode_names(name_bytes, size_t(bone_count), geometry.skin_bone_names)) {
            error = "invalid cooked geometry bone-name stream";
            return false;
        }
        if (has_inverse_bind) {
            if (inverse_bind_stride->second != "64" ||
                !detail::decode_floats(package, "skin_inverse_bind_matrices_b64",
                    size_t(bone_count) * 16, geometry.skin_inverse_bind_matrices)) {
                error = "invalid cooked geometry inverse bind matrix stream";
                return false;
            }
            for (size_t bone = 0; bone < size_t(bone_count); ++bone) {
                if (!detail::valid_inverse_bind_matrix(geometry.skin_inverse_bind_matrices.data() + bone * 16)) {
                    error = "invalid cooked geometry inverse bind matrix";
                    return false;
                }
            }
        }
        for (size_t vertex = 0; vertex < size_t(vertices); ++vertex) {
            float total_weight = 0.0f;
            for (size_t influence = 0; influence < 4; ++influence) {
                const size_t offset = vertex * 4 + influence;
                const float weight = geometry.skin_weights[offset];
                if (weight < 0.0f || (weight > 0.0f && geometry.skin_indices[offset] >= bone_count)) {
                    error = "cooked geometry contains an invalid bone influence";
                    return false;
                }
                total_weight += weight;
            }
            if (!std::isfinite(total_weight) || std::abs(total_weight - 1.0f) > 0.005f) {
                error = "cooked geometry bone weights are not normalized";
                return false;
            }
        }
    }
    if (!detail::parse_geometry_subsets(package, geometry, error)) return false;
    if (!detail::parse_slot_materials(package, geometry.material_slots, geometry.slot_materials, error) ||
        !detail::parse_slot_material_names(package, geometry.material_slots, geometry.slot_materials,
            geometry.slot_material_names, error) ||
        !detail::parse_slot_textures(package, geometry.slot_materials, geometry.texture_sections, error)) {
        return false;
    }
    if (!detail::parse_geometry_scene(package, geometry, error)) return false;
    if (!detail::parse_geometry_morphs(package, geometry, vertices, error)) return false;
    const auto joint_count_section = package.sections.find("skin_joints");
    const auto parents_stride = package.sections.find("skin_joint_parent_stride");
    const auto parents_data = package.sections.find("skin_joint_parents_b64");
    const auto rest_stride = package.sections.find("skin_joint_rest_stride");
    const auto rest_data = package.sections.find("skin_joint_rest_b64");
    const auto joint_names_data = package.sections.find("skin_joint_names_b64");
    const auto cluster_map_stride = package.sections.find("skin_cluster_joints_stride");
    const auto cluster_map_data = package.sections.find("skin_cluster_joints_b64");
    const bool has_rig = joint_count_section != package.sections.end();
    if ((has_rig && !has_rig_format) ||
        (parents_stride != package.sections.end()) != has_rig ||
        (parents_data != package.sections.end()) != has_rig ||
        (rest_stride != package.sections.end()) != has_rig ||
        (rest_data != package.sections.end()) != has_rig ||
        (joint_names_data != package.sections.end()) != has_rig ||
        (cluster_map_stride != package.sections.end()) != has_rig ||
        (cluster_map_data != package.sections.end()) != has_rig ||
        (has_rig && !has_skin) || (has_inverse_bind && !has_rig)) {
        error = "incomplete cooked geometry rig hierarchy";
        return false;
    }
    if (has_rig) {
        uint64_t joint_count = 0;
        std::vector<int32_t> parents;
        std::vector<float> rest_values;
        if (!detail::parse_count(package, "skin_joints", joint_count) || joint_count == 0 ||
            joint_count > MAX_GEOMETRY_RIG_NODES ||
            parents_stride->second != "4" || rest_stride->second != "40" || cluster_map_stride->second != "4" ||
            !detail::decode_i32(package, "skin_joint_parents_b64", size_t(joint_count), parents) ||
            !detail::decode_floats(package, "skin_joint_rest_b64", size_t(joint_count) * 10, rest_values) ||
            !detail::decode_u32(package, "skin_cluster_joints_b64", geometry.skin_bone_names.size(), geometry.skin_cluster_joints)) {
            error = "invalid cooked geometry rig counts or streams";
            return false;
        }
        std::vector<uint8_t> encoded_names;
        std::vector<std::string> joint_names;
        if (!detail::decode_base64(joint_names_data->second, encoded_names) ||
            !detail::decode_names(encoded_names, size_t(joint_count), joint_names)) {
            error = "invalid cooked geometry joint-name stream";
            return false;
        }
        std::vector<bool> cluster_seen(size_t(joint_count), false);
        geometry.skin_joints.reserve(size_t(joint_count));
        for (size_t joint_index = 0; joint_index < size_t(joint_count); ++joint_index) {
            const int32_t parent = parents[joint_index];
            if (parent < -1 || parent >= int32_t(joint_index)) {
                error = "cooked geometry rig is not parent ordered";
                return false;
            }
            CookedGeometry::SkinJoint joint;
            joint.name = std::move(joint_names[joint_index]);
            joint.parent_index = parent;
            std::copy_n(rest_values.data() + joint_index * 10, 10, joint.rest_local.begin());
            const float rotation_length = std::sqrt(joint.rest_local[3] * joint.rest_local[3] +
                joint.rest_local[4] * joint.rest_local[4] + joint.rest_local[5] * joint.rest_local[5] +
                joint.rest_local[6] * joint.rest_local[6]);
            if (!(rotation_length > 0.99f && rotation_length < 1.01f) ||
                joint.rest_local[7] == 0.0f || joint.rest_local[8] == 0.0f || joint.rest_local[9] == 0.0f) {
                error = "cooked geometry has an invalid joint rest transform";
                return false;
            }
            geometry.skin_joints.push_back(std::move(joint));
        }
        for (size_t cluster = 0; cluster < geometry.skin_cluster_joints.size(); ++cluster) {
            const uint32_t joint = geometry.skin_cluster_joints[cluster];
            if (joint >= joint_count || cluster_seen[joint] ||
                geometry.skin_joints[joint].name != geometry.skin_bone_names[cluster]) {
                error = "cooked geometry cluster map does not match its joint hierarchy";
                return false;
            }
            cluster_seen[joint] = true;
            geometry.skin_joints[joint].cluster_index = int32_t(cluster);
        }

    }
    if (!detail::parse_geometry_animations(package, geometry, error)) return false;
    return true;
}

inline bool load_cooked_geometry(const std::string& path, CookedGeometry& geometry,
    std::string& error, probe::BinaryPackageReadCancellationCheck cancellation_check = {}) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        error = "cooked geometry package is missing";
        return false;
    }
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > probe::PackageIndex::MAX_PACKAGE_BYTES) {
        error = "cooked geometry package exceeds its bound";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    size_t bytes_read = 0;
    while (bytes_read < bytes.size()) {
        if (cancellation_check && cancellation_check(bytes_read, bytes.size())) {
            error = "cooked geometry package read cancelled";
            return false;
        }
        const size_t chunk = std::min(probe::BINARY_PACKAGE_READ_CHUNK_BYTES,
            bytes.size() - bytes_read);
        input.read(reinterpret_cast<char*>(bytes.data() + bytes_read),
            static_cast<std::streamsize>(chunk));
        if (input.gcount() != static_cast<std::streamsize>(chunk)) {
            error = "cooked geometry package read failed";
            return false;
        }
        bytes_read += chunk;
    }
    if (cancellation_check && cancellation_check(bytes_read, bytes.size())) {
        error = "cooked geometry package read cancelled";
        return false;
    }
    return load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, error);
}

inline bool load_cooked_geometry_asset(const std::string& path, CookedGeometry& geometry,
    std::string& error, probe::BinaryPackageReadCancellationCheck cancellation_check = {}) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cooked geometry package is missing";
        return false;
    }
    std::array<uint8_t, 4> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (input.gcount() != static_cast<std::streamsize>(magic.size()) ||
        magic != std::array<uint8_t, 4>{'E', 'L', 'P', 'K'}) {
        if (!load_cooked_geometry(path, geometry, error, std::move(cancellation_check))) return false;
        if (geometry.texture_sections.empty()) return true;
        error = "cooked slot textures need an ELPK bundle";
        return false;
    }

    const probe::BinaryPackageIndex index = probe::read_binary_package_index(path);
    if (!index.valid) {
        error = index.error.empty() ? "invalid binary cooked geometry package" : index.error;
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!probe::read_binary_package_section(path, index, "mesh", bytes, error,
            std::move(cancellation_check)) ||
        !load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, error)) return false;
    for (const std::string& section : geometry.texture_sections) {
        const auto entry = std::find_if(index.sections.begin(), index.sections.end(),
            [&section](const probe::BinaryPackageSection& candidate) { return candidate.name == section; });
        if (entry == index.sections.end()) {
            error = "cooked slot texture section is missing from its bundle";
            return false;
        }
        geometry.texture_checksums.push_back(entry->checksum);
    }
    return true;
}

} // namespace elisa::assets
#include "cooked_geometry_morphs.h"
#include "cooked_geometry_scene.h"
#include "cooked_geometry_animations.h"
