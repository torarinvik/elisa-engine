#pragma once

// Bounded reader for the engine's normalized triangle-mesh package. FBX is
// cooked offline; the runtime accepts only this versioned, validated format.
#include "virtual_package.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
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
    const char* configured_root = std::getenv("ELISA_PROJECT_ROOT");
    const std::filesystem::path root = configured_root != nullptr && configured_root[0] != '\0'
        ? std::filesystem::canonical(std::filesystem::u8path(configured_root), filesystem_error)
        : std::filesystem::canonical(std::filesystem::current_path(filesystem_error), filesystem_error);
    if (filesystem_error || !std::filesystem::is_directory(root, filesystem_error) || filesystem_error) return false;
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

} // namespace detail

inline bool load_cooked_geometry(const std::string& path, CookedGeometry& geometry,
    std::string& error) {
    const probe::PackageIndex package = probe::read_package_index(path);
    if (!package.valid) {
        error = package.error.empty() ? "invalid cooked geometry package" : package.error;
        return false;
    }
    const auto format = package.sections.find("format");
    if (format == package.sections.end() || format->second != "elisa-cooked-v2") {
        error = "unsupported cooked geometry format";
        return false;
    }
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
        if (!detail::decode_base64(skin_names->second, name_bytes)) {
            error = "invalid cooked geometry bone-name stream";
            return false;
        }
        size_t name_offset = 0;
        geometry.skin_bone_names.reserve(size_t(bone_count));
        for (size_t bone = 0; bone < size_t(bone_count); ++bone) {
            if (name_bytes.size() - name_offset < 4) {
                error = "truncated cooked geometry bone name";
                return false;
            }
            const uint32_t length = uint32_t(name_bytes[name_offset]) |
                (uint32_t(name_bytes[name_offset + 1]) << 8) |
                (uint32_t(name_bytes[name_offset + 2]) << 16) |
                (uint32_t(name_bytes[name_offset + 3]) << 24);
            name_offset += 4;
            if (length > name_bytes.size() - name_offset) {
                error = "cooked geometry bone name exceeds its stream";
                return false;
            }
            geometry.skin_bone_names.emplace_back(
                reinterpret_cast<const char*>(name_bytes.data() + name_offset), length);
            name_offset += length;
        }
        if (name_offset != name_bytes.size()) {
            error = "cooked geometry bone-name stream has trailing bytes";
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
    return true;
}

} // namespace elisa::assets
