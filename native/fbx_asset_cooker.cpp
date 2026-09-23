#include "fbx_asset_import.h"
#include "fbx_asset_cooker_geometry.h"
#include "mikktspace_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <locale>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

constexpr size_t MAX_PACKAGE_BYTES = size_t(64) * 1024 * 1024;
constexpr size_t MAX_LINE_BYTES = size_t(16) * 1024 * 1024;
constexpr uint32_t SLOT_MATERIAL_DOUBLE_SIDED_FLAG = 1u;

bool safe_asset_key(const std::string& value) {
    if (value.empty() || value.size() > 4096 || value.front() == '/' ||
        value.find('\\') != std::string::npos || value.find_first_of("\r\n") != std::string::npos ||
        value.find('\0') != std::string::npos) return false;
    size_t start = 0;
    while (start < value.size()) {
        const size_t end = value.find('/', start);
        const std::string component = value.substr(start,
            end == std::string::npos ? std::string::npos : end - start);
        if (component.empty() || component == "." || component == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return true;
}

void append_u32_le(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.push_back(uint8_t(value));
    bytes.push_back(uint8_t(value >> 8));
    bytes.push_back(uint8_t(value >> 16));
    bytes.push_back(uint8_t(value >> 24));
}

std::vector<uint8_t> float_bytes(const std::vector<float>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(float));
    for (float value : values) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        append_u32_le(bytes, bits);
    }
    return bytes;
}

std::vector<uint8_t> index_bytes(const std::vector<uint32_t>& values) {
    std::vector<uint8_t> bytes;
    bytes.reserve(values.size() * sizeof(uint32_t));
    for (uint32_t value : values) append_u32_le(bytes, value);
    return bytes;
}

std::vector<uint8_t> skin_name_bytes(const std::vector<std::string>& names) {
    std::vector<uint8_t> bytes;
    for (const std::string& name : names) {
        if (name.size() > std::numeric_limits<uint32_t>::max()) return {};
        append_u32_le(bytes, uint32_t(name.size()));
        bytes.insert(bytes.end(), name.begin(), name.end());
    }
    return bytes;
}

std::vector<uint8_t> slot_material_bytes(const std::vector<elisa::assets::FbxMaterialData>& materials) {
    std::vector<uint8_t> bytes;
    bytes.reserve(materials.size() * 48);
    for (const auto& material : materials) {
        std::vector<float> factors(material.base_color.begin(), material.base_color.end());
        factors.push_back(material.metallic);
        factors.push_back(material.roughness);
        factors.insert(factors.end(), material.emissive.begin(), material.emissive.end());
        factors.push_back(material.alpha_cutoff);
        const std::vector<uint8_t> factor_bytes = float_bytes(factors);
        bytes.insert(bytes.end(), factor_bytes.begin(), factor_bytes.end());
        append_u32_le(bytes, material.alpha_mode);
        append_u32_le(bytes, material.double_sided ? SLOT_MATERIAL_DOUBLE_SIDED_FLAG : 0);
    }
    return bytes;
}

std::vector<uint8_t> skin_joint_name_bytes(const std::vector<elisa::assets::FbxSkinJoint>& joints) {
    std::vector<uint8_t> bytes;
    for (const auto& joint : joints) {
        if (joint.name.size() > std::numeric_limits<uint32_t>::max()) return {};
        append_u32_le(bytes, uint32_t(joint.name.size()));
        bytes.insert(bytes.end(), joint.name.begin(), joint.name.end());
    }
    return bytes;
}

std::vector<uint8_t> animation_name_bytes(const std::string& name) {
    if (name.size() > std::numeric_limits<uint32_t>::max()) return {};
    std::vector<uint8_t> bytes;
    append_u32_le(bytes, uint32_t(name.size()));
    bytes.insert(bytes.end(), name.begin(), name.end());
    return bytes;
}

size_t base64_size(size_t byte_count) {
    if (byte_count > std::numeric_limits<size_t>::max() - 2) return std::numeric_limits<size_t>::max();
    const size_t groups = (byte_count + 2) / 3;
    if (groups > std::numeric_limits<size_t>::max() / 4) return std::numeric_limits<size_t>::max();
    return groups * 4;
}

std::string base64(const std::vector<uint8_t>& bytes) {
    static constexpr char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    encoded.reserve(base64_size(bytes.size()));
    for (size_t index = 0; index < bytes.size(); index += 3) {
        const uint32_t first = bytes[index];
        const uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const uint32_t word = (first << 16) | (second << 8) | third;
        encoded.push_back(alphabet[(word >> 18) & 63]);
        encoded.push_back(alphabet[(word >> 12) & 63]);
        encoded.push_back(index + 1 < bytes.size() ? alphabet[(word >> 6) & 63] : '=');
        encoded.push_back(index + 2 < bytes.size() ? alphabet[word & 63] : '=');
    }
    return encoded;
}

bool cook(const std::filesystem::path& source, const std::string& asset_key,
    const std::filesystem::path& output, const std::string& source_sha256, size_t max_triangles,
    const std::string& selected_mesh_name) {
    if (!safe_asset_key(asset_key)) {
        std::fprintf(stderr, "unsafe asset key; use a project-relative path without `..`\n");
        return false;
    }
    if (source_sha256.size() != 64 || source_sha256.find_first_not_of("0123456789abcdef") != std::string::npos) {
        std::fprintf(stderr, "source SHA-256 must be 64 lowercase hexadecimal characters\n");
        return false;
    }
    elisa::assets::FbxImportResult asset = elisa::assets::import_fbx(source, true, selected_mesh_name);
    if (!asset.ok) {
        std::fprintf(stderr, "FBX cook failed: %s\n", asset.error.c_str());
        return false;
    }
    auto& mesh = asset.primary_mesh;
    if (mesh.positions.empty() || mesh.positions.size() % 3 != 0 ||
        mesh.normals.size() != mesh.positions.size() || mesh.uvs.size() != mesh.positions.size() / 3 * 2 ||
        mesh.indices.empty() || mesh.indices.size() % 3 != 0) {
        std::fprintf(stderr, "FBX importer returned incomplete triangle geometry\n");
        return false;
    }
    if (!elisa::assets::cooker::simplify_geometry(mesh, max_triangles)) return false;
    if (!elisa::assets::cooker::optimize_vertex_cache(mesh)) return false;
    elisa::assets::MikkGeometry tangent_geometry;
    if (!elisa::assets::generate_mikktspace_geometry(mesh.positions, mesh.normals, mesh.uvs,
        mesh.indices, tangent_geometry)) {
        std::fprintf(stderr, "FBX tangent-frame generation failed for the cooked mesh\n");
        return false;
    }
    std::vector<uint32_t> split_skin_indices;
    std::vector<float> split_skin_weights;
    if (!mesh.skin_indices.empty()) {
        split_skin_indices.reserve(tangent_geometry.source_vertices.size() * 4);
        split_skin_weights.reserve(tangent_geometry.source_vertices.size() * 4);
        for (uint32_t source : tangent_geometry.source_vertices) {
            if (source >= mesh.skin_indices.size() / 4 || source >= mesh.skin_weights.size() / 4) {
                std::fprintf(stderr, "MikkTSpace seam split returned an invalid skin source vertex\n");
                return false;
            }
            split_skin_indices.insert(split_skin_indices.end(), mesh.skin_indices.begin() + size_t(source) * 4,
                mesh.skin_indices.begin() + size_t(source) * 4 + 4);
            split_skin_weights.insert(split_skin_weights.end(), mesh.skin_weights.begin() + size_t(source) * 4,
                mesh.skin_weights.begin() + size_t(source) * 4 + 4);
        }
        mesh.skin_indices.swap(split_skin_indices);
        mesh.skin_weights.swap(split_skin_weights);
    }
    mesh.positions.swap(tangent_geometry.positions);
    mesh.normals.swap(tangent_geometry.normals);
    mesh.uvs.swap(tangent_geometry.uvs);
    mesh.tangents.swap(tangent_geometry.tangents);
    mesh.indices.swap(tangent_geometry.indices);
    if (!elisa::assets::cooker::optimize_vertex_fetch(mesh)) return false;
    for (uint32_t index : mesh.indices) {
        if (index >= mesh.positions.size() / 3) {
            std::fprintf(stderr, "FBX importer returned an out-of-range mesh index\n");
            return false;
        }
    }
    const std::vector<uint8_t> positions = float_bytes(mesh.positions);
    const std::vector<uint8_t> normals = float_bytes(mesh.normals);
    const std::vector<uint8_t> uvs = float_bytes(mesh.uvs);
    const std::vector<uint8_t> tangents = float_bytes(mesh.tangents);
    const std::vector<uint8_t> indices = index_bytes(mesh.indices);
    std::vector<uint32_t> subset_words;
    subset_words.reserve(mesh.subsets.size() * 3);
    uint32_t next_subset_index = 0;
    for (const auto& subset : mesh.subsets) {
        if (subset.index_start != next_subset_index || subset.index_count == 0 ||
            subset.index_count % 3 != 0 || subset.material_slot >= mesh.material_slots ||
            subset.index_count > mesh.indices.size() - subset.index_start) {
            std::fprintf(stderr, "FBX cooker produced invalid material subset ranges\n");
            return false;
        }
        subset_words.push_back(subset.index_start);
        subset_words.push_back(subset.index_count);
        subset_words.push_back(subset.material_slot);
        next_subset_index += subset.index_count;
    }
    if (mesh.material_slots == 0 || mesh.material_slots > elisa::assets::detail::MAX_FBX_MATERIAL_SLOTS ||
        mesh.subsets.empty() || next_subset_index != mesh.indices.size()) {
        std::fprintf(stderr, "FBX cooker material subsets do not cover the index stream\n");
        return false;
    }
    if (!mesh.materials.empty() && mesh.materials.size() != mesh.material_slots) {
        std::fprintf(stderr, "FBX cooker material records do not match the material slots\n");
        return false;
    }
    const std::vector<uint8_t> subset_bytes = index_bytes(subset_words);
    std::vector<std::string> slot_material_names;
    slot_material_names.reserve(mesh.materials.size());
    for (const auto& material : mesh.materials) slot_material_names.push_back(material.name);
    const std::vector<uint8_t> slot_materials = slot_material_bytes(mesh.materials);
    const std::vector<uint8_t> slot_material_names_bytes = skin_name_bytes(slot_material_names);
    const std::vector<uint8_t> skin_indices = index_bytes(mesh.skin_indices);
    const std::vector<uint8_t> skin_weights = float_bytes(mesh.skin_weights);
    const std::vector<uint8_t> skin_names = skin_name_bytes(mesh.skin_bone_names);
    std::vector<int32_t> skin_joint_parents;
    std::vector<float> skin_joint_rest;
    std::vector<uint32_t> skin_cluster_joints = mesh.skin_cluster_joints;
    std::vector<std::string> skin_joint_names;
    for (const auto& joint : mesh.skin_joints) {
        skin_joint_parents.push_back(joint.parent_index);
        skin_joint_names.push_back(joint.name);
        skin_joint_rest.insert(skin_joint_rest.end(), std::begin(joint.rest_local), std::end(joint.rest_local));
    }
    std::vector<uint32_t> skin_joint_parent_words;
    for (int32_t parent : skin_joint_parents) skin_joint_parent_words.push_back(uint32_t(parent));
    const std::vector<uint8_t> skin_joint_parent_bytes = index_bytes(skin_joint_parent_words);
    const std::vector<uint8_t> skin_joint_rest_bytes = float_bytes(skin_joint_rest);
    const std::vector<uint8_t> skin_joint_names_bytes = skin_name_bytes(skin_joint_names);
    const std::vector<uint8_t> skin_cluster_joint_bytes = index_bytes(skin_cluster_joints);
    const size_t positions_encoded = base64_size(positions.size());
    const size_t normals_encoded = base64_size(normals.size());
    const size_t uvs_encoded = base64_size(uvs.size());
    const size_t tangents_encoded = base64_size(tangents.size());
    const size_t indices_encoded = base64_size(indices.size());
    const size_t subsets_encoded = base64_size(subset_bytes.size());
    const size_t slot_materials_encoded = base64_size(slot_materials.size());
    const size_t slot_material_names_encoded = base64_size(slot_material_names_bytes.size());
    const size_t skin_indices_encoded = base64_size(skin_indices.size());
    const size_t skin_weights_encoded = base64_size(skin_weights.size());
    const size_t skin_names_encoded = base64_size(skin_names.size());
    const size_t skin_joint_parents_encoded = base64_size(skin_joint_parent_bytes.size());
    const size_t skin_joint_rest_encoded = base64_size(skin_joint_rest_bytes.size());
    const size_t skin_joint_names_encoded = base64_size(skin_joint_names_bytes.size());
    const size_t skin_cluster_joints_encoded = base64_size(skin_cluster_joint_bytes.size());
    const bool has_skin = !mesh.skin_bone_names.empty();
    if (has_skin && (mesh.skin_indices.size() != mesh.positions.size() / 3 * 4 ||
        mesh.skin_weights.size() != mesh.skin_indices.size() || skin_names.empty() ||
        mesh.skin_joints.empty() || mesh.skin_joints.size() > 64 ||
        mesh.skin_cluster_joints.size() != mesh.skin_bone_names.size() ||
        skin_joint_parents.size() != mesh.skin_joints.size() ||
        skin_joint_rest.size() != mesh.skin_joints.size() * 10 || skin_joint_names_bytes.empty() ||
        skin_cluster_joint_bytes.empty())) {
        std::fprintf(stderr, "FBX importer returned incomplete skin streams\n");
        return false;
    }
    const size_t max_payload = MAX_LINE_BYTES - 14;
    if (positions_encoded > max_payload || normals_encoded > max_payload ||
        uvs_encoded > max_payload || tangents_encoded > max_payload || indices_encoded > max_payload ||
        subsets_encoded > max_payload || slot_materials_encoded > max_payload ||
        slot_material_names_encoded > max_payload ||
        (has_skin && (skin_indices_encoded > max_payload || skin_weights_encoded > max_payload ||
            skin_names_encoded > max_payload || skin_joint_parents_encoded > max_payload ||
            skin_joint_rest_encoded > max_payload || skin_joint_names_encoded > max_payload ||
            skin_cluster_joints_encoded > max_payload))) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package line limit; simplify or split the source asset\n");
        return false;
    }
    const size_t metadata_budget = 2048 + asset_key.size();
    if (positions_encoded > MAX_PACKAGE_BYTES - metadata_budget ||
        normals_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded ||
        uvs_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded - normals_encoded ||
        tangents_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded - normals_encoded - uvs_encoded ||
        indices_encoded > MAX_PACKAGE_BYTES - metadata_budget - positions_encoded - normals_encoded - uvs_encoded - tangents_encoded) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package size limit; simplify or split the source asset\n");
        return false;
    }

    std::ostringstream package;
    package.imbue(std::locale::classic());
    package << std::setprecision(std::numeric_limits<float>::max_digits10);
    package << "format=" << (has_skin ? "elisa-cooked-v3\n" : "elisa-cooked-v2\n")
        << "source=" << asset_key << "\n"
        << "source_sha256=" << source_sha256 << "\n"
        << "triangles=" << mesh.indices.size() / 3 << "\n"
        << "positions=" << mesh.positions.size() / 3 << "\n"
        << "indices=" << mesh.indices.size() << "\n"
        << "bounds_min=" << mesh.bounds_min[0] << ',' << mesh.bounds_min[1] << ',' << mesh.bounds_min[2] << "\n"
        << "bounds_max=" << mesh.bounds_max[0] << ',' << mesh.bounds_max[1] << ',' << mesh.bounds_max[2] << "\n"
        << "position_stride=12\nnormal_stride=12\nuv_stride=8\ntangent_stride=16\nindex_stride=4\n"
        << "material_slots=" << mesh.material_slots << "\n"
        << "subset_count=" << mesh.subsets.size() << "\nsubset_stride=12\n"
        << "subsets_b64=" << base64(subset_bytes) << "\n"
        << "positions_b64=" << base64(positions) << "\n"
        << "normals_b64=" << base64(normals) << "\n"
        << "uvs_b64=" << base64(uvs) << "\n"
        << "tangents_b64=" << base64(tangents) << "\n"
        << "indices_b64=" << base64(indices) << "\n";
    if (!mesh.materials.empty()) {
        package << "slot_material_stride=48\n"
            << "slot_materials_b64=" << base64(slot_materials) << "\n"
            << "slot_material_names_b64=" << base64(slot_material_names_bytes) << "\n";
    }
    if (has_skin) {
        package << "skin_bones=" << mesh.skin_bone_names.size() << "\n"
            << "skin_indices_stride=16\nskin_weights_stride=16\n"
            << "skin_indices_b64=" << base64(skin_indices) << "\n"
            << "skin_weights_b64=" << base64(skin_weights) << "\n"
            << "skin_names_b64=" << base64(skin_names) << "\n"
            << "skin_joints=" << mesh.skin_joints.size() << "\n"
            << "skin_joint_parent_stride=4\nskin_joint_rest_stride=40\nskin_cluster_joints_stride=4\n"
            << "skin_joint_parents_b64=" << base64(skin_joint_parent_bytes) << "\n"
            << "skin_joint_rest_b64=" << base64(skin_joint_rest_bytes) << "\n"
            << "skin_joint_names_b64=" << base64(skin_joint_names_bytes) << "\n"
            << "skin_cluster_joints_b64=" << base64(skin_cluster_joint_bytes) << "\n"
            << "animation_clips=" << mesh.animation_clips.size() << "\n";
        for (size_t clip_index = 0; clip_index < mesh.animation_clips.size(); ++clip_index) {
            const auto& clip = mesh.animation_clips[clip_index];
            const std::string prefix = "animation_" + std::to_string(clip_index) + "_";
            package << prefix << "name_b64=" << base64(animation_name_bytes(clip.name)) << "\n"
                << prefix << "duration_seconds=" << clip.duration_seconds << "\n"
                << prefix << "sample_rate=" << clip.sample_rate << "\n"
                << prefix << "frames=" << clip.frame_count << "\n"
                << prefix << "transform_stride=40\n"
                << prefix << "samples_b64=" << base64(float_bytes(clip.local_transforms)) << "\n";
        }
    }
    const std::string bytes = package.str();
    if (bytes.size() > MAX_PACKAGE_BYTES) {
        std::fprintf(stderr, "FBX geometry exceeds the cooked package size limit\n");
        return false;
    }

    std::error_code filesystem_error;
    std::filesystem::create_directories(output.parent_path(), filesystem_error);
    if (filesystem_error) {
        std::fprintf(stderr, "cannot create package output directory: %s\n", filesystem_error.message().c_str());
        return false;
    }
    std::filesystem::path temporary = output;
    temporary += ".tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) {
            std::fprintf(stderr, "cannot open temporary package output\n");
            return false;
        }
        stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        if (!stream) {
            stream.close();
            std::filesystem::remove(temporary, filesystem_error);
            std::fprintf(stderr, "failed while writing cooked package\n");
            return false;
        }
    }
    std::filesystem::rename(temporary, output, filesystem_error);
    if (filesystem_error) {
        std::filesystem::remove(temporary);
        std::fprintf(stderr, "cannot publish cooked package: %s\n", filesystem_error.message().c_str());
        return false;
    }
    std::printf("cooked %s -> %s (%zu triangles, %zu vertices, %zu bytes)\n",
        asset_key.c_str(), output.string().c_str(), mesh.indices.size() / 3,
        mesh.positions.size() / 3, bytes.size());
    return true;
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 9 || (argc - 9) % 2 != 0 || std::string(argv[1]) != "--source" ||
        std::string(argv[3]) != "--asset-path" || std::string(argv[5]) != "--output" ||
        std::string(argv[7]) != "--sha256") {
        std::fprintf(stderr, "usage: fbx_asset_cooker --source FILE --asset-path PROJECT_RELATIVE_PATH --output FILE --sha256 HEX [--max-triangles COUNT] [--mesh-name NAME]\n");
        return 2;
    }
    size_t max_triangles = 0;
    std::string selected_mesh_name;
    bool saw_triangle_limit = false;
    bool saw_mesh_name = false;
    for (int argument = 9; argument < argc; argument += 2) {
        const std::string option = argv[argument];
        if (option == "--max-triangles" && !saw_triangle_limit) {
            char* end = nullptr;
            const unsigned long long value = std::strtoull(argv[argument + 1], &end, 10);
            if (end == argv[argument + 1] || *end != '\0' || value == 0 || value > 1000000) {
                std::fprintf(stderr, "max triangles must be an integer in [1, 1000000]\n");
                return 2;
            }
            max_triangles = size_t(value);
            saw_triangle_limit = true;
        } else if (option == "--mesh-name" && !saw_mesh_name) {
            selected_mesh_name = argv[argument + 1];
            if (selected_mesh_name.empty() || selected_mesh_name.size() > 512) {
                std::fprintf(stderr, "mesh name must contain 1 to 512 bytes\n");
                return 2;
            }
            saw_mesh_name = true;
        } else {
            std::fprintf(stderr, "unknown or duplicate FBX cooker option: %s\n", option.c_str());
            return 2;
        }
    }
    return cook(argv[2], argv[4], argv[6], argv[8], max_triangles, selected_mesh_name) ? 0 : 1;
}
