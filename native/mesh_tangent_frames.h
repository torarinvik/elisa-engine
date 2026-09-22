#pragma once

// Deterministic indexed tangent-frame generation for cooked triangle meshes.
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace elisa::assets {

inline bool generate_tangent_frames(const std::vector<float>& positions,
    const std::vector<float>& normals, const std::vector<float>& uvs,
    const std::vector<uint32_t>& indices, std::vector<float>& tangents) {
    if (positions.empty() || positions.size() % 3 != 0 || normals.size() != positions.size() ||
        uvs.size() != positions.size() / 3 * 2 || indices.empty() || indices.size() % 3 != 0) return false;
    const size_t vertex_count = positions.size() / 3;
    using Vector = std::array<double, 3>;
    std::vector<Vector> tangent_sum(vertex_count, Vector{});
    std::vector<Vector> bitangent_sum(vertex_count, Vector{});
    std::vector<Vector> geometric_normal_sum(vertex_count, Vector{});
    const auto subtract = [](const float* a, const float* b) -> Vector {
        return {double(a[0]) - b[0], double(a[1]) - b[1], double(a[2]) - b[2]};
    };
    for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        const uint32_t i0 = indices[triangle];
        const uint32_t i1 = indices[triangle + 1];
        const uint32_t i2 = indices[triangle + 2];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) return false;
        const Vector edge1 = subtract(&positions[size_t(i1) * 3], &positions[size_t(i0) * 3]);
        const Vector edge2 = subtract(&positions[size_t(i2) * 3], &positions[size_t(i0) * 3]);
        const Vector face_normal = {edge1[1] * edge2[2] - edge1[2] * edge2[1],
            edge1[2] * edge2[0] - edge1[0] * edge2[2],
            edge1[0] * edge2[1] - edge1[1] * edge2[0]};
        const double face_length = std::sqrt(face_normal[0] * face_normal[0] +
            face_normal[1] * face_normal[1] + face_normal[2] * face_normal[2]);
        if (face_length > 1.0e-20 && std::isfinite(face_length)) {
            for (uint32_t vertex : {i0, i1, i2}) {
                for (size_t axis = 0; axis < 3; ++axis) {
                    geometric_normal_sum[vertex][axis] += face_normal[axis];
                }
            }
        }
        const double du1 = double(uvs[size_t(i1) * 2]) - uvs[size_t(i0) * 2];
        const double dv1 = double(uvs[size_t(i1) * 2 + 1]) - uvs[size_t(i0) * 2 + 1];
        const double du2 = double(uvs[size_t(i2) * 2]) - uvs[size_t(i0) * 2];
        const double dv2 = double(uvs[size_t(i2) * 2 + 1]) - uvs[size_t(i0) * 2 + 1];
        const double determinant = du1 * dv2 - du2 * dv1;
        if (std::abs(determinant) <= 1.0e-20 || !std::isfinite(determinant)) continue;
        const double inverse = 1.0 / determinant;
        const Vector tangent = {(edge1[0] * dv2 - edge2[0] * dv1) * inverse,
            (edge1[1] * dv2 - edge2[1] * dv1) * inverse,
            (edge1[2] * dv2 - edge2[2] * dv1) * inverse};
        const Vector bitangent = {(edge2[0] * du1 - edge1[0] * du2) * inverse,
            (edge2[1] * du1 - edge1[1] * du2) * inverse,
            (edge2[2] * du1 - edge1[2] * du2) * inverse};
        for (uint32_t vertex : {i0, i1, i2}) {
            for (size_t axis = 0; axis < 3; ++axis) {
                tangent_sum[vertex][axis] += tangent[axis];
                bitangent_sum[vertex][axis] += bitangent[axis];
            }
        }
    }

    tangents.resize(vertex_count * 4);
    for (size_t vertex = 0; vertex < vertex_count; ++vertex) {
        const double nx = normals[vertex * 3];
        const double ny = normals[vertex * 3 + 1];
        const double nz = normals[vertex * 3 + 2];
        double normal_length = std::sqrt(nx * nx + ny * ny + nz * nz);
        Vector normal = {nx, ny, nz};
        if (!(normal_length > 1.0e-20) || !std::isfinite(normal_length)) {
            normal = geometric_normal_sum[vertex];
            normal_length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
                normal[2] * normal[2]);
        }
        if (!(normal_length > 1.0e-20) || !std::isfinite(normal_length)) return false;
        for (double& component : normal) component /= normal_length;
        const double projection = normal[0] * tangent_sum[vertex][0] +
            normal[1] * tangent_sum[vertex][1] + normal[2] * tangent_sum[vertex][2];
        Vector tangent = {tangent_sum[vertex][0] - normal[0] * projection,
            tangent_sum[vertex][1] - normal[1] * projection,
            tangent_sum[vertex][2] - normal[2] * projection};
        double tangent_length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
            tangent[2] * tangent[2]);
        if (!(tangent_length > 1.0e-20) || !std::isfinite(tangent_length)) {
            const Vector axis = std::abs(normal[0]) < 0.75 ? Vector{1.0, 0.0, 0.0} : Vector{0.0, 1.0, 0.0};
            tangent = {normal[1] * axis[2] - normal[2] * axis[1],
                normal[2] * axis[0] - normal[0] * axis[2],
                normal[0] * axis[1] - normal[1] * axis[0]};
            tangent_length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
                tangent[2] * tangent[2]);
        }
        if (!(tangent_length > 1.0e-20) || !std::isfinite(tangent_length)) return false;
        for (double& component : tangent) component /= tangent_length;
        const Vector cross = {normal[1] * tangent[2] - normal[2] * tangent[1],
            normal[2] * tangent[0] - normal[0] * tangent[2],
            normal[0] * tangent[1] - normal[1] * tangent[0]};
        const double orientation = cross[0] * bitangent_sum[vertex][0] +
            cross[1] * bitangent_sum[vertex][1] + cross[2] * bitangent_sum[vertex][2];
        const float handedness = orientation < 0.0 ? -1.0f : 1.0f;
        tangents[vertex * 4] = float(tangent[0]);
        tangents[vertex * 4 + 1] = float(tangent[1]);
        tangents[vertex * 4 + 2] = float(tangent[2]);
        tangents[vertex * 4 + 3] = handedness;
        for (size_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(tangents[vertex * 4 + axis])) return false;
        }
    }
    return true;
}

} // namespace elisa::assets
