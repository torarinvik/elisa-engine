#pragma once

// Bounded reader for collision-only geometry cooked from static glTF scenes.
#include "cooked_package_fields.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace elisa::assets {

inline constexpr uint32_t MAX_COLLISION_VERTICES = 65'536;
inline constexpr uint32_t MAX_COLLISION_INDICES = 196'608;
inline constexpr size_t MAX_COLLISION_GEOMETRY_BYTES = 8u * 1024u * 1024u;
inline constexpr size_t MAX_COLLISION_PACKAGE_BYTES = 64u * 1024u * 1024u;
inline constexpr float MAX_COLLISION_COORDINATE = 10'000.0f;

struct CookedCollisionGeometry {
    std::string shape;
    std::vector<float> positions;
    std::vector<uint32_t> indices;
};

inline bool collision_has_volume(const std::vector<float>& positions) {
    if (positions.size() < 12 || positions.size() % 3 != 0) return false;
    const size_t vertex_count = positions.size() / 3;
    double extent = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        float minimum = positions[axis], maximum = minimum;
        for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
            const float value = positions[vertex * 3 + axis];
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        extent = std::max(extent, double(maximum) - minimum);
    }
    if (!(extent > 1.0e-6)) return false;
    const double area_limit = extent * extent * extent * extent * 1.0e-12;
    const double volume_limit = extent * extent * extent * 1.0e-9;
    const double p0[3] = {positions[0], positions[1], positions[2]};
    double line[3]{};
    bool found_line = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_line; ++vertex) {
        for (size_t axis = 0; axis < 3; ++axis) {
            line[axis] = double(positions[vertex * 3 + axis]) - p0[axis];
        }
        const double length2 = line[0] * line[0] + line[1] * line[1] + line[2] * line[2];
        found_line = length2 > extent * extent * 1.0e-12;
    }
    if (!found_line) return false;
    double normal[3]{};
    bool found_plane = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_plane; ++vertex) {
        const double plane[3] = {
            double(positions[vertex * 3]) - p0[0],
            double(positions[vertex * 3 + 1]) - p0[1],
            double(positions[vertex * 3 + 2]) - p0[2],
        };
        normal[0] = line[1] * plane[2] - line[2] * plane[1];
        normal[1] = line[2] * plane[0] - line[0] * plane[2];
        normal[2] = line[0] * plane[1] - line[1] * plane[0];
        found_plane = normal[0] * normal[0] + normal[1] * normal[1] +
            normal[2] * normal[2] > area_limit;
    }
    if (!found_plane) return false;
    for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
        const double delta[3] = {
            double(positions[vertex * 3]) - p0[0],
            double(positions[vertex * 3 + 1]) - p0[1],
            double(positions[vertex * 3 + 2]) - p0[2],
        };
        const double signed_volume = normal[0] * delta[0] +
            normal[1] * delta[1] + normal[2] * delta[2];
        if (std::abs(signed_volume) > volume_limit) return true;
    }
    return false;
}

inline bool parse_cooked_collision_geometry_bytes(const uint8_t* bytes, size_t byte_count,
    CookedCollisionGeometry& geometry, std::string& error) {
    if (bytes == nullptr || byte_count == 0 || byte_count > MAX_COLLISION_PACKAGE_BYTES) {
        error = "collision package exceeds its byte bound";
        return false;
    }
    const probe::PackageIndex package = probe::parse_package_index(
        std::string(reinterpret_cast<const char*>(bytes), byte_count));
    if (!package.valid) {
        error = package.error.empty() ? "invalid collision package" : package.error;
        return false;
    }
    const auto format = package.sections.find("format");
    const auto shape = package.sections.find("shape");
    const auto source = package.sections.find("source");
    const auto source_sha = package.sections.find("source_sha256");
    if (format == package.sections.end() || format->second != "elisa-physics-collision-v1" ||
        shape == package.sections.end() ||
        (shape->second != "convex_hull" && shape->second != "triangle_mesh") ||
        source == package.sections.end() || source_sha == package.sections.end() ||
        source_sha->second.size() != 64 ||
        source_sha->second.find_first_not_of("0123456789abcdef") != std::string::npos) {
        error = "unsupported collision package format or shape";
        return false;
    }
    uint64_t vertex_count = 0, index_count = 0;
    if (!detail::parse_count(package, "vertices", vertex_count) || vertex_count < 3 ||
        vertex_count > MAX_COLLISION_VERTICES ||
        !detail::parse_count(package, "indices", index_count) || index_count < 3 ||
        index_count > MAX_COLLISION_INDICES || index_count % 3 != 0 ||
        package.sections.count("position_stride") == 0 ||
        package.sections.at("position_stride") != "12" ||
        package.sections.count("index_stride") == 0 ||
        package.sections.at("index_stride") != "4" ||
        (shape->second == "convex_hull" && vertex_count < 4)) {
        error = "collision package counts or strides are invalid";
        return false;
    }
    const size_t geometry_bytes = size_t(vertex_count) * 3 * sizeof(float) +
        size_t(index_count) * sizeof(uint32_t);
    if (geometry_bytes > MAX_COLLISION_GEOMETRY_BYTES) {
        error = "collision geometry exceeds its memory bound";
        return false;
    }
    CookedCollisionGeometry parsed;
    parsed.shape = shape->second;
    if (!detail::decode_floats(package, "positions_b64", vertex_count * 3, parsed.positions) ||
        !detail::decode_u32(package, "indices_b64", index_count, parsed.indices)) {
        error = "collision position or index stream is malformed";
        return false;
    }
    for (float coordinate : parsed.positions) {
        if (std::abs(coordinate) > MAX_COLLISION_COORDINATE) {
            error = "collision coordinate exceeds its runtime bound";
            return false;
        }
    }
    for (size_t offset = 0; offset < parsed.indices.size(); offset += 3) {
        const uint32_t a = parsed.indices[offset], b = parsed.indices[offset + 1],
            c = parsed.indices[offset + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count ||
            a == b || b == c || a == c) {
            error = "collision index is outside its stream or repeats a triangle vertex";
            return false;
        }
        const double ab[3] = {
            double(parsed.positions[size_t(b) * 3]) - parsed.positions[size_t(a) * 3],
            double(parsed.positions[size_t(b) * 3 + 1]) - parsed.positions[size_t(a) * 3 + 1],
            double(parsed.positions[size_t(b) * 3 + 2]) - parsed.positions[size_t(a) * 3 + 2],
        };
        const double ac[3] = {
            double(parsed.positions[size_t(c) * 3]) - parsed.positions[size_t(a) * 3],
            double(parsed.positions[size_t(c) * 3 + 1]) - parsed.positions[size_t(a) * 3 + 1],
            double(parsed.positions[size_t(c) * 3 + 2]) - parsed.positions[size_t(a) * 3 + 2],
        };
        const double nx = ab[1] * ac[2] - ab[2] * ac[1];
        const double ny = ab[2] * ac[0] - ab[0] * ac[2];
        const double nz = ab[0] * ac[1] - ab[1] * ac[0];
        if (nx * nx + ny * ny + nz * nz <= 1.0e-20) {
            error = "collision package contains a degenerate triangle";
            return false;
        }
    }
    if (parsed.shape == "convex_hull" && !collision_has_volume(parsed.positions)) {
        error = "convex collision geometry has no volume";
        return false;
    }
    geometry = std::move(parsed);
    return true;
}

inline bool load_cooked_collision_geometry_asset(const std::string& path,
    CookedCollisionGeometry& geometry, std::string& error) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        error = "collision package is missing";
        return false;
    }
    std::array<uint8_t, 4> magic{};
    input.read(reinterpret_cast<char*>(magic.data()), static_cast<std::streamsize>(magic.size()));
    if (input.gcount() == static_cast<std::streamsize>(magic.size()) &&
        magic == std::array<uint8_t, 4>{'E', 'L', 'P', 'K'}) {
        const probe::BinaryPackageIndex index = probe::read_binary_package_index(path);
        if (!index.valid) {
            error = index.error.empty() ? "invalid collision bundle" : index.error;
            return false;
        }
        std::vector<uint8_t> bytes;
        return probe::read_binary_package_section(path, index, "collision", bytes, error) &&
            parse_cooked_collision_geometry_bytes(bytes.data(), bytes.size(), geometry, error);
    }
    input.clear();
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0 || static_cast<uint64_t>(size) > MAX_COLLISION_PACKAGE_BYTES) {
        error = "collision package exceeds its file bound";
        return false;
    }
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    input.seekg(0);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!input && !bytes.empty()) {
        error = "collision package read failed";
        return false;
    }
    return parse_cooked_collision_geometry_bytes(bytes.data(), bytes.size(), geometry, error);
}

} // namespace elisa::assets
