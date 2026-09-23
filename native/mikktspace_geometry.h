#pragma once

// MikkTSpace-compatible tangent generation with the vertex splits required by
// its unindexed per-corner callback contract.
#include "mikktspace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <utility>
#include <vector>

namespace elisa::assets {

struct MikkGeometry {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<uint32_t> indices;
    std::vector<uint32_t> source_vertices;
};

namespace mikktspace_detail {

struct Input {
    const std::vector<float>* positions;
    const std::vector<float>* normals;
    const std::vector<float>* uvs;
    const std::vector<uint32_t>* indices;
    const std::vector<size_t>* output_corner_indices;
    std::vector<std::array<float, 4>>* corner_frames;
};

inline uint32_t vertex_at(const SMikkTSpaceContext* context, int face, int corner) {
    const auto* input = static_cast<const Input*>(context->m_pUserData);
    return (*input->indices)[size_t(face) * 3 + size_t(corner)];
}

inline int face_count(const SMikkTSpaceContext* context) {
    const auto* input = static_cast<const Input*>(context->m_pUserData);
    return int(input->indices->size() / 3);
}

inline int vertices_per_face(const SMikkTSpaceContext*, int) { return 3; }

inline void get_position(const SMikkTSpaceContext* context, float output[], int face, int corner) {
    const auto* input = static_cast<const Input*>(context->m_pUserData);
    const size_t offset = size_t(vertex_at(context, face, corner)) * 3;
    std::memcpy(output, input->positions->data() + offset, sizeof(float) * 3);
}

inline void get_normal(const SMikkTSpaceContext* context, float output[], int face, int corner) {
    const auto* input = static_cast<const Input*>(context->m_pUserData);
    const size_t offset = size_t(vertex_at(context, face, corner)) * 3;
    const float* source = input->normals->data() + offset;
    const double length = std::sqrt(double(source[0]) * source[0] +
        double(source[1]) * source[1] + double(source[2]) * source[2]);
    if (length > 1.0e-20 && std::isfinite(length)) {
        output[0] = float(source[0] / length);
        output[1] = float(source[1] / length);
        output[2] = float(source[2] / length);
    } else {
        output[0] = output[1] = output[2] = 0.0f;
    }
}

inline void get_uv(const SMikkTSpaceContext* context, float output[], int face, int corner) {
    const auto* input = static_cast<const Input*>(context->m_pUserData);
    const size_t offset = size_t(vertex_at(context, face, corner)) * 2;
    output[0] = (*input->uvs)[offset];
    output[1] = (*input->uvs)[offset + 1];
}

inline std::array<float, 4> fallback_frame(const float normal[]) {
    double nx = normal[0], ny = normal[1], nz = normal[2];
    const double normal_length = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!(normal_length > 1.0e-20) || !std::isfinite(normal_length)) {
        nx = 0.0;
        ny = 1.0;
        nz = 0.0;
    } else {
        nx /= normal_length;
        ny /= normal_length;
        nz /= normal_length;
    }

    // Cross the normalized normal with its least-aligned cardinal axis so the
    // result is stable and comfortably away from a zero-length tangent.
    double tx = 0.0, ty = 0.0, tz = 0.0;
    if (std::abs(nx) <= std::abs(ny) && std::abs(nx) <= std::abs(nz)) {
        ty = -nz;
        tz = ny;
    } else if (std::abs(ny) <= std::abs(nz)) {
        tx = nz;
        tz = -nx;
    } else {
        tx = -ny;
        ty = nx;
    }
    const double tangent_length = std::sqrt(tx * tx + ty * ty + tz * tz);
    return {float(tx / tangent_length), float(ty / tangent_length),
        float(tz / tangent_length), 1.0f};
}

inline void set_frame(const SMikkTSpaceContext* context, const float tangent[], float sign,
        int face, int corner) {
    auto* input = static_cast<Input*>(context->m_pUserData);
    const size_t callback_corner = size_t(face) * 3 + size_t(corner);
    auto& frame = (*input->corner_frames)[(*input->output_corner_indices)[callback_corner]];
    const double length = std::sqrt(double(tangent[0]) * tangent[0] +
        double(tangent[1]) * tangent[1] + double(tangent[2]) * tangent[2]);
    if (!(length > 1.0e-20) || !std::isfinite(length) || !std::isfinite(sign) ||
        std::abs(std::abs(sign) - 1.0f) > 1.0e-4f) return;
    const uint32_t source = vertex_at(context, face, corner);
    const size_t normal_offset = size_t(source) * 3;
    const double dot = double(tangent[0]) * (*input->normals)[normal_offset] +
        double(tangent[1]) * (*input->normals)[normal_offset + 1] +
        double(tangent[2]) * (*input->normals)[normal_offset + 2];
    if (!std::isfinite(dot) || std::abs(dot / length) > 0.02) return;
    frame = {float(tangent[0] / length), float(tangent[1] / length),
        float(tangent[2] / length), sign < 0.0f ? -1.0f : 1.0f};
}

struct VertexKey {
    uint32_t source;
    std::array<uint32_t, 4> frame;

    bool operator<(const VertexKey& other) const {
        return source < other.source || (source == other.source && frame < other.frame);
    }
};

inline uint32_t float_bits(float value) {
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits;
}

} // namespace mikktspace_detail

inline bool generate_mikktspace_geometry(const std::vector<float>& positions,
        const std::vector<float>& normals, const std::vector<float>& uvs,
        const std::vector<uint32_t>& indices, MikkGeometry& output,
        size_t max_output_vertices = std::numeric_limits<size_t>::max()) {
    const size_t vertex_count = positions.size() / 3;
    if (positions.empty() || positions.size() % 3 != 0 || normals.size() != positions.size() ||
        uvs.size() != vertex_count * 2 || indices.empty() || indices.size() % 3 != 0 ||
        indices.size() / 3 > size_t(std::numeric_limits<int>::max()) || max_output_vertices == 0) return false;
    for (float value : positions) if (!std::isfinite(value)) return false;
    for (float value : normals) if (!std::isfinite(value)) return false;
    for (float value : uvs) if (!std::isfinite(value)) return false;
    for (uint32_t index : indices) if (index >= vertex_count) return false;

    std::vector<float> fixed_normals = normals;
    std::vector<std::array<double, 3>> generated_normals(vertex_count, std::array<double, 3>{});
    std::vector<uint32_t> mikktspace_indices;
    std::vector<size_t> output_corner_indices;
    mikktspace_indices.reserve(indices.size());
    output_corner_indices.reserve(indices.size());
    for (size_t triangle = 0; triangle < indices.size(); triangle += 3) {
        const uint32_t a = indices[triangle], b = indices[triangle + 1], c = indices[triangle + 2];
        const double e1[3] = {positions[size_t(b) * 3] - positions[size_t(a) * 3],
            positions[size_t(b) * 3 + 1] - positions[size_t(a) * 3 + 1],
            positions[size_t(b) * 3 + 2] - positions[size_t(a) * 3 + 2]};
        const double e2[3] = {positions[size_t(c) * 3] - positions[size_t(a) * 3],
            positions[size_t(c) * 3 + 1] - positions[size_t(a) * 3 + 1],
            positions[size_t(c) * 3 + 2] - positions[size_t(a) * 3 + 2]};
        const std::array<double, 3> face = {e1[1] * e2[2] - e1[2] * e2[1],
            e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
        const double geometry_area_squared = face[0] * face[0] + face[1] * face[1] + face[2] * face[2];
        const double edge1_squared = e1[0] * e1[0] + e1[1] * e1[1] + e1[2] * e1[2];
        const double edge2_squared = e2[0] * e2[0] + e2[1] * e2[1] + e2[2] * e2[2];
        for (uint32_t index : {a, b, c}) {
            for (size_t axis = 0; axis < 3; ++axis) generated_normals[index][axis] += face[axis];
        }

        const size_t uv_a = size_t(a) * 2, uv_b = size_t(b) * 2, uv_c = size_t(c) * 2;
        const double du1 = double(uvs[uv_b]) - uvs[uv_a];
        const double dv1 = double(uvs[uv_b + 1]) - uvs[uv_a + 1];
        const double du2 = double(uvs[uv_c]) - uvs[uv_a];
        const double dv2 = double(uvs[uv_c + 1]) - uvs[uv_a + 1];
        const double uv_area = du1 * dv2 - dv1 * du2;
        const double uv_scale = std::max({std::abs(du1), std::abs(dv1), std::abs(du2), std::abs(dv2)});
        const bool valid_geometry = std::isfinite(geometry_area_squared) &&
            geometry_area_squared > edge1_squared * edge2_squared * 1.0e-16;
        const bool valid_uv = std::isfinite(uv_area) && std::isfinite(uv_scale) && uv_scale > 0.0 &&
            std::abs(uv_area) > std::max(1.0e-30, uv_scale * uv_scale * 1.0e-12);
        if (valid_geometry && valid_uv) {
            for (size_t corner = 0; corner < 3; ++corner) {
                mikktspace_indices.push_back(indices[triangle + corner]);
                output_corner_indices.push_back(triangle + corner);
            }
        }
    }
    for (size_t vertex = 0; vertex < vertex_count; ++vertex) {
        double length = std::sqrt(double(normals[vertex * 3]) * normals[vertex * 3] +
            double(normals[vertex * 3 + 1]) * normals[vertex * 3 + 1] +
            double(normals[vertex * 3 + 2]) * normals[vertex * 3 + 2]);
        if (length > 1.0e-20 && std::isfinite(length)) {
            for (size_t axis = 0; axis < 3; ++axis) {
                fixed_normals[vertex * 3 + axis] = float(normals[vertex * 3 + axis] / length);
            }
            continue;
        }
        const auto& normal = generated_normals[vertex];
        length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
        if (length > 1.0e-20 && std::isfinite(length)) {
            for (size_t axis = 0; axis < 3; ++axis) {
                fixed_normals[vertex * 3 + axis] = float(normal[axis] / length);
            }
        } else {
            // Unreferenced FBX control points and fully degenerate triangles
            // have no geometric normal to recover. They still need a finite
            // frame for a complete cooked vertex stream.
            fixed_normals[vertex * 3] = 0.0f;
            fixed_normals[vertex * 3 + 1] = 1.0f;
            fixed_normals[vertex * 3 + 2] = 0.0f;
        }
    }

    std::vector<std::array<float, 4>> corner_frames(indices.size());
    for (size_t corner = 0; corner < indices.size(); ++corner) {
        const uint32_t source = indices[corner];
        corner_frames[corner] = mikktspace_detail::fallback_frame(
            fixed_normals.data() + size_t(source) * 3);
    }
    mikktspace_detail::Input input{&positions, &fixed_normals, &uvs, &mikktspace_indices,
        &output_corner_indices, &corner_frames};
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = mikktspace_detail::face_count;
    interface.m_getNumVerticesOfFace = mikktspace_detail::vertices_per_face;
    interface.m_getPosition = mikktspace_detail::get_position;
    interface.m_getNormal = mikktspace_detail::get_normal;
    interface.m_getTexCoord = mikktspace_detail::get_uv;
    interface.m_setTSpaceBasic = mikktspace_detail::set_frame;
    SMikkTSpaceContext context{&interface, &input};
    if (!mikktspace_indices.empty() && !genTangSpaceDefault(&context)) return false;

    MikkGeometry result;
    result.indices.reserve(indices.size());
    const size_t reserve_vertices = std::min({indices.size(), vertex_count, max_output_vertices});
    result.source_vertices.reserve(reserve_vertices);
    result.positions.reserve(reserve_vertices * 3);
    result.normals.reserve(reserve_vertices * 3);
    result.uvs.reserve(reserve_vertices * 2);
    result.tangents.reserve(reserve_vertices * 4);
    std::map<mikktspace_detail::VertexKey, uint32_t> vertices;
    for (size_t corner = 0; corner < indices.size(); ++corner) {
        const auto& frame = corner_frames[corner];
        const double tangent_length = std::sqrt(double(frame[0]) * frame[0] +
            double(frame[1]) * frame[1] + double(frame[2]) * frame[2]);
        if (!std::isfinite(tangent_length) || std::abs(tangent_length - 1.0) > 0.02 ||
            !std::isfinite(frame[3]) || std::abs(std::abs(frame[3]) - 1.0f) > 1.0e-4f) return false;
        const uint32_t source = indices[corner];
        const double tangent_normal_dot = double(frame[0]) * fixed_normals[size_t(source) * 3] +
            double(frame[1]) * fixed_normals[size_t(source) * 3 + 1] +
            double(frame[2]) * fixed_normals[size_t(source) * 3 + 2];
        if (!std::isfinite(tangent_normal_dot) || std::abs(tangent_normal_dot) > 0.02) return false;
        const mikktspace_detail::VertexKey key{source, {mikktspace_detail::float_bits(frame[0]),
            mikktspace_detail::float_bits(frame[1]), mikktspace_detail::float_bits(frame[2]),
            mikktspace_detail::float_bits(frame[3])}};
        auto found = vertices.find(key);
        if (found == vertices.end()) {
            if (result.source_vertices.size() >= max_output_vertices ||
                result.source_vertices.size() >= std::numeric_limits<uint32_t>::max()) return false;
            const uint32_t next = uint32_t(result.source_vertices.size());
            found = vertices.emplace(key, next).first;
            result.source_vertices.push_back(source);
            result.positions.insert(result.positions.end(), positions.begin() + size_t(source) * 3,
                positions.begin() + size_t(source) * 3 + 3);
            result.normals.insert(result.normals.end(), fixed_normals.begin() + size_t(source) * 3,
                fixed_normals.begin() + size_t(source) * 3 + 3);
            result.uvs.insert(result.uvs.end(), uvs.begin() + size_t(source) * 2,
                uvs.begin() + size_t(source) * 2 + 2);
            result.tangents.insert(result.tangents.end(), frame.begin(), frame.end());
        }
        result.indices.push_back(found->second);
    }
    output = std::move(result);
    return true;
}

} // namespace elisa::assets
