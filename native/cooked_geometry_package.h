#pragma once

// Bounded reader for the engine's normalized triangle-mesh package. FBX is
// cooked offline; the runtime accepts only this versioned, validated format.
#include "bundle_dependencies.h"
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
#include <vector>

namespace elisa::assets {

struct CookedGeometry {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    std::vector<std::string> skin_bone_names;
    std::vector<uint32_t> skin_indices;
    std::vector<float> skin_weights;
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
    };
    std::vector<SkinJoint> skin_joints;
    std::vector<uint32_t> skin_cluster_joints;
    std::vector<AnimationClip> animation_clips;
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

inline bool decode_base64(const std::string& text, std::vector<uint8_t>& bytes) {
    if (text.empty() || text.size() % 4 != 0 || text.size() > 64u * 1024u * 1024u) return false;
    auto value = [](char character) -> int {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26;
        if (character >= '0' && character <= '9') return character - '0' + 52;
        if (character == '+') return 62;
        if (character == '/') return 63;
        return -1;
    };
    bytes.clear();
    bytes.reserve(text.size() / 4 * 3);
    for (size_t offset = 0; offset < text.size(); offset += 4) {
        const bool last = offset + 4 == text.size();
        const int a = value(text[offset]);
        const int b = value(text[offset + 1]);
        const bool pad_c = text[offset + 2] == '=';
        const bool pad_d = text[offset + 3] == '=';
        const int c = pad_c ? 0 : value(text[offset + 2]);
        const int d = pad_d ? 0 : value(text[offset + 3]);
        if (a < 0 || b < 0 || c < 0 || d < 0 || (pad_c && !pad_d) ||
            ((pad_c || pad_d) && !last) || (pad_c && (b & 15) != 0) ||
            (pad_d && !pad_c && (c & 3) != 0)) return false;
        const uint32_t word = (uint32_t(a) << 18) | (uint32_t(b) << 12) |
            (uint32_t(c) << 6) | uint32_t(d);
        bytes.push_back(uint8_t(word >> 16));
        if (!pad_c) bytes.push_back(uint8_t(word >> 8));
        if (!pad_d) bytes.push_back(uint8_t(word));
    }
    return true;
}

inline bool parse_count(const probe::PackageIndex& package, const char* key, uint64_t& value) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || found->second.empty()) return false;
    value = 0;
    for (char character : found->second) {
        if (character < '0' || character > '9') return false;
        const uint64_t digit = uint64_t(character - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10) return false;
        value = value * 10 + digit;
    }
    return true;
}

inline bool decode_floats(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<float>& output) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || count > 16u * 1024u * 1024u / sizeof(float)) return false;
    std::vector<uint8_t> bytes;
    if (!decode_base64(found->second, bytes) || bytes.size() != count * sizeof(float)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        const uint32_t bits = uint32_t(bytes[index * 4]) |
            (uint32_t(bytes[index * 4 + 1]) << 8) |
            (uint32_t(bytes[index * 4 + 2]) << 16) |
            (uint32_t(bytes[index * 4 + 3]) << 24);
        std::memcpy(&output[index], &bits, sizeof(bits));
        if (!std::isfinite(output[index])) return false;
    }
    return true;
}

inline bool decode_u32(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<uint32_t>& output) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || count > 16u * 1024u * 1024u / sizeof(uint32_t)) return false;
    std::vector<uint8_t> bytes;
    if (!decode_base64(found->second, bytes) || bytes.size() != count * sizeof(uint32_t)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        const size_t offset = index * 4;
        output[index] = uint32_t(bytes[offset]) | (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) | (uint32_t(bytes[offset + 3]) << 24);
    }
    return true;
}

inline bool decode_i32(const probe::PackageIndex& package, const char* key,
    size_t count, std::vector<int32_t>& output) {
    std::vector<uint32_t> raw;
    if (!decode_u32(package, key, count, raw)) return false;
    output.resize(count);
    for (size_t index = 0; index < count; ++index) {
        std::memcpy(&output[index], &raw[index], sizeof(int32_t));
    }
    return true;
}

inline bool decode_names(const std::vector<uint8_t>& bytes, size_t count,
    std::vector<std::string>& names) {
    size_t offset = 0;
    names.clear();
    names.reserve(count);
    for (size_t index = 0; index < count; ++index) {
        if (bytes.size() - offset < 4) return false;
        const uint32_t length = uint32_t(bytes[offset]) |
            (uint32_t(bytes[offset + 1]) << 8) |
            (uint32_t(bytes[offset + 2]) << 16) |
            (uint32_t(bytes[offset + 3]) << 24);
        offset += 4;
        if (length > bytes.size() - offset) return false;
        names.emplace_back(reinterpret_cast<const char*>(bytes.data() + offset), length);
        offset += length;
    }
    return offset == bytes.size();
}

inline bool parse_finite_float(const probe::PackageIndex& package, const std::string& key,
    float& value) {
    const auto found = package.sections.find(key);
    if (found == package.sections.end() || found->second.empty()) return false;
    char* end = nullptr;
    const float parsed = std::strtof(found->second.c_str(), &end);
    if (end != found->second.c_str() + found->second.size() || !std::isfinite(parsed)) return false;
    value = parsed;
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
    if (!detail::decode_floats(package, "positions_b64", size_t(vertices) * 3, geometry.positions) ||
        !detail::decode_floats(package, "normals_b64", size_t(vertices) * 3, geometry.normals) ||
        !detail::decode_floats(package, "uvs_b64", size_t(vertices) * 2, geometry.uvs)) {
        error = "invalid cooked geometry vertex streams";
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
    const auto encoded_indices = package.sections.find("indices_b64");
    std::vector<uint8_t> index_bytes;
    if (encoded_indices == package.sections.end() ||
        !detail::decode_base64(encoded_indices->second, index_bytes) || index_bytes.size() != size_t(indices) * 4) {
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
    const bool has_skin = skin_bones != package.sections.end();
    if ((skin_index_stride != package.sections.end()) != has_skin ||
        (skin_weight_stride != package.sections.end()) != has_skin ||
        (skin_indices != package.sections.end()) != has_skin ||
        (skin_weights != package.sections.end()) != has_skin ||
        (skin_names != package.sections.end()) != has_skin) {
        error = "incomplete cooked geometry skin stream";
        return false;
    }
    if (has_skin) {
        uint64_t bone_count = 0;
        if (!detail::parse_count(package, "skin_bones", bone_count) || bone_count == 0 || bone_count > 64 ||
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

    const auto joint_count_section = package.sections.find("skin_joints");
    const auto parents_stride = package.sections.find("skin_joint_parent_stride");
    const auto parents_data = package.sections.find("skin_joint_parents_b64");
    const auto rest_stride = package.sections.find("skin_joint_rest_stride");
    const auto rest_data = package.sections.find("skin_joint_rest_b64");
    const auto joint_names_data = package.sections.find("skin_joint_names_b64");
    const auto cluster_map_stride = package.sections.find("skin_cluster_joints_stride");
    const auto cluster_map_data = package.sections.find("skin_cluster_joints_b64");
    const bool has_rig = joint_count_section != package.sections.end();
    if (has_rig_format != has_rig ||
        (parents_stride != package.sections.end()) != has_rig ||
        (parents_data != package.sections.end()) != has_rig ||
        (rest_stride != package.sections.end()) != has_rig ||
        (rest_data != package.sections.end()) != has_rig ||
        (joint_names_data != package.sections.end()) != has_rig ||
        (cluster_map_stride != package.sections.end()) != has_rig ||
        (cluster_map_data != package.sections.end()) != has_rig ||
        (has_rig && !has_skin)) {
        error = "incomplete cooked geometry rig hierarchy";
        return false;
    }
    if (has_rig) {
        uint64_t joint_count = 0;
        std::vector<int32_t> parents;
        std::vector<float> rest_values;
        if (!detail::parse_count(package, "skin_joints", joint_count) || joint_count == 0 || joint_count > 64 ||
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

        uint64_t clip_count = 0;
        if (!detail::parse_count(package, "animation_clips", clip_count) || clip_count > 8) {
            error = "invalid cooked geometry animation clip count";
            return false;
        }
        size_t total_sample_floats = 0;
        for (size_t clip_index = 0; clip_index < size_t(clip_count); ++clip_index) {
            const std::string prefix = "animation_" + std::to_string(clip_index) + "_";
            const auto encoded_name = package.sections.find(prefix + "name_b64");
            const auto sample_rate = package.sections.find(prefix + "sample_rate");
            const auto frame_count_section = package.sections.find(prefix + "frames");
            const auto transform_stride = package.sections.find(prefix + "transform_stride");
            const auto samples = package.sections.find(prefix + "samples_b64");
            float duration = 0.0f;
            uint64_t rate = 0;
            uint64_t frames = 0;
            if (encoded_name == package.sections.end() || sample_rate == package.sections.end() ||
                frame_count_section == package.sections.end() || transform_stride == package.sections.end() ||
                samples == package.sections.end() || transform_stride->second != "40" ||
                !detail::parse_finite_float(package, prefix + "duration_seconds", duration) || duration <= 0.0f ||
                !detail::parse_count(package, (prefix + "sample_rate").c_str(), rate) || rate == 0 || rate > 120 ||
                !detail::parse_count(package, (prefix + "frames").c_str(), frames) || frames < 2 || frames > 3601 ||
                size_t(joint_count) > (2'000'000 - total_sample_floats) / 10 / size_t(frames)) {
                error = "invalid cooked geometry animation metadata";
                return false;
            }
            const size_t sample_floats = size_t(joint_count) * size_t(frames) * 10;
            CookedGeometry::AnimationClip clip;
            std::vector<uint8_t> clip_name_bytes;
            std::vector<std::string> names;
            if (!detail::decode_base64(encoded_name->second, clip_name_bytes) ||
                !detail::decode_names(clip_name_bytes, 1, names) ||
                !detail::decode_floats(package, (prefix + "samples_b64").c_str(), sample_floats,
                    clip.local_transforms)) {
                error = "invalid cooked geometry animation samples or name";
                return false;
            }
            clip.name = std::move(names[0]);
            if (clip.name.empty()) {
                error = "cooked geometry animation name is empty";
                return false;
            }
            clip.duration_seconds = duration;
            clip.sample_rate = uint32_t(rate);
            clip.frame_count = uint32_t(frames);
            for (size_t offset = 0; offset < clip.local_transforms.size(); offset += 10) {
                const float* transform = clip.local_transforms.data() + offset;
                const float rotation_length = std::sqrt(transform[3] * transform[3] + transform[4] * transform[4] +
                    transform[5] * transform[5] + transform[6] * transform[6]);
                if (!(rotation_length > 0.99f && rotation_length < 1.01f) ||
                    transform[7] == 0.0f || transform[8] == 0.0f || transform[9] == 0.0f) {
                    error = "cooked geometry animation has an invalid transform sample";
                    return false;
                }
            }
            total_sample_floats += sample_floats;
            geometry.animation_clips.push_back(std::move(clip));
        }
    }
    return true;
}

inline bool load_cooked_geometry(const std::string& path, CookedGeometry& geometry,
    std::string& error) {
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
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input) {
        error = "cooked geometry package read failed";
        return false;
    }
    return load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, error);
}

inline bool load_cooked_geometry_asset(const std::string& path, CookedGeometry& geometry,
    std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "cooked geometry package is missing";
        return false;
    }
    std::array<uint8_t, 4> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (input.gcount() != static_cast<std::streamsize>(magic.size()) ||
        magic != std::array<uint8_t, 4>{'E', 'L', 'P', 'K'}) {
        return load_cooked_geometry(path, geometry, error);
    }

    const probe::BinaryPackageIndex index = probe::read_binary_package_index(path);
    if (!index.valid) {
        error = index.error.empty() ? "invalid binary cooked geometry package" : index.error;
        return false;
    }
    std::vector<uint8_t> bytes;
    if (!probe::read_binary_package_section(path, index, "mesh", bytes, error)) return false;
    return load_cooked_geometry_bytes(bytes.data(), bytes.size(), geometry, error);
}

} // namespace elisa::assets
