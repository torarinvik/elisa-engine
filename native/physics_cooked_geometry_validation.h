#pragma once

#include "cooked_geometry_package.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace elisa_physics_internal {

inline bool cooked_geometry_has_non_degenerate_triangle(
        const elisa::assets::CookedGeometry& geometry) {
    if (geometry.positions.size() < 9 || geometry.positions.size() % 3 != 0 ||
        geometry.indices.size() < 3 || geometry.indices.size() % 3 != 0) return false;
    const size_t vertex_count = geometry.positions.size() / 3;
    double extent = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        float minimum = geometry.positions[axis];
        float maximum = minimum;
        if (!std::isfinite(minimum)) return false;
        for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
            const float value = geometry.positions[vertex * 3 + axis];
            if (!std::isfinite(value)) return false;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        extent = std::max(extent, double(maximum) - minimum);
    }
    if (extent <= 1.0e-6) return false;
    const double area_limit = extent * extent * extent * extent * 1.0e-12;
    for (size_t offset = 0; offset < geometry.indices.size(); offset += 3) {
        const uint32_t a = geometry.indices[offset];
        const uint32_t b = geometry.indices[offset + 1];
        const uint32_t c = geometry.indices[offset + 2];
        if (a >= vertex_count || b >= vertex_count || c >= vertex_count ||
            a == b || b == c || a == c) return false;
        const double abx = geometry.positions[size_t(b) * 3] - geometry.positions[size_t(a) * 3];
        const double aby = geometry.positions[size_t(b) * 3 + 1] - geometry.positions[size_t(a) * 3 + 1];
        const double abz = geometry.positions[size_t(b) * 3 + 2] - geometry.positions[size_t(a) * 3 + 2];
        const double acx = geometry.positions[size_t(c) * 3] - geometry.positions[size_t(a) * 3];
        const double acy = geometry.positions[size_t(c) * 3 + 1] - geometry.positions[size_t(a) * 3 + 1];
        const double acz = geometry.positions[size_t(c) * 3 + 2] - geometry.positions[size_t(a) * 3 + 2];
        const double nx = aby * acz - abz * acy;
        const double ny = abz * acx - abx * acz;
        const double nz = abx * acy - aby * acx;
        if (nx * nx + ny * ny + nz * nz <= area_limit) return false;
    }
    return true;
}

inline bool cooked_geometry_has_volume(const elisa::assets::CookedGeometry& geometry) {
    if (geometry.positions.size() < 12 || geometry.positions.size() % 3 != 0) return false;
    const size_t vertex_count = geometry.positions.size() / 3;
    double extent = 0.0;
    for (size_t axis = 0; axis < 3; ++axis) {
        float minimum = geometry.positions[axis];
        float maximum = minimum;
        for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
            const float value = geometry.positions[vertex * 3 + axis];
            if (!std::isfinite(value)) return false;
            minimum = std::min(minimum, value);
            maximum = std::max(maximum, value);
        }
        extent = std::max(extent, double(maximum) - minimum);
    }
    if (!(extent > 1.0e-6)) return false;
    const double area_limit = extent * extent * extent * extent * 1.0e-12;
    const double volume_limit = extent * extent * extent * 1.0e-9;
    const double p0[3] = {geometry.positions[0], geometry.positions[1], geometry.positions[2]};
    double v1[3] = {};
    bool found_line = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_line; ++vertex) {
        for (size_t axis = 0; axis < 3; ++axis) {
            v1[axis] = double(geometry.positions[vertex * 3 + axis]) - p0[axis];
        }
        const double length2 = v1[0] * v1[0] + v1[1] * v1[1] + v1[2] * v1[2];
        found_line = length2 > extent * extent * 1.0e-12;
    }
    if (!found_line) return false;
    double normal[3] = {};
    bool found_plane = false;
    for (size_t vertex = 1; vertex < vertex_count && !found_plane; ++vertex) {
        const double v2[3] = {
            double(geometry.positions[vertex * 3]) - p0[0],
            double(geometry.positions[vertex * 3 + 1]) - p0[1],
            double(geometry.positions[vertex * 3 + 2]) - p0[2],
        };
        normal[0] = v1[1] * v2[2] - v1[2] * v2[1];
        normal[1] = v1[2] * v2[0] - v1[0] * v2[2];
        normal[2] = v1[0] * v2[1] - v1[1] * v2[0];
        found_plane = normal[0] * normal[0] + normal[1] * normal[1] +
            normal[2] * normal[2] > area_limit;
    }
    if (!found_plane) return false;
    for (size_t vertex = 1; vertex < vertex_count; ++vertex) {
        const double v3[3] = {
            double(geometry.positions[vertex * 3]) - p0[0],
            double(geometry.positions[vertex * 3 + 1]) - p0[1],
            double(geometry.positions[vertex * 3 + 2]) - p0[2],
        };
        const double signed_volume = normal[0] * v3[0] + normal[1] * v3[1] + normal[2] * v3[2];
        if (std::abs(signed_volume) > volume_limit) return true;
    }
    return false;
}

} // namespace elisa_physics_internal
