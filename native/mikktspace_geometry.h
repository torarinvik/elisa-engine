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

inline void set_frame(const SMikkTSpaceContext* context, const float tangent[], float sign,
        int face, int corner) {
    auto* input = static_cast<Input*>(context->m_pUserData);
    auto& frame = (*input->corner_frames)[size_t(face) * 3 + size_t(corner)];
    frame = {tangent[0], tangent[1], tangent[2], sign};
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
        const std::vector<uint32_t>& indices, MikkGeometry& output) {
    const size_t vertex_count = positions.size() / 3;
    if (positions.empty() || positions.size() % 3 != 0 || normals.size() != positions.size() ||
        uvs.size() != vertex_count * 2 || indices.empty() || indices.size() % 3 != 0 ||
        indices.size() / 3 > size_t(std::numeric_limits<int>::max())) return false;
    for (float value : positions) if (!std::isfinite(value)) return false;
    for (float value : normals) if (!std::isfinite(value)) return false;
    for (float value : uvs) if (!std::isfinite(value)) return false;
    for (uint32_t index : indices) if (index >= vertex_count) return false;

    std::vector<float> fixed_normals = normals;
    std::vector<std::array<double, 3>> generated_normals(vertex_count, std::array<double, 3>{});
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
        for (uint32_t index : {a, b, c}) {
            for (size_t axis = 0; axis < 3; ++axis) generated_normals[index][axis] += face[axis];
        }
    }
    for (size_t vertex = 0; vertex < vertex_count; ++vertex) {
        double length = std::sqrt(double(normals[vertex * 3]) * normals[vertex * 3] +
            double(normals[vertex * 3 + 1]) * normals[vertex * 3 + 1] +
            double(normals[vertex * 3 + 2]) * normals[vertex * 3 + 2]);
        if (!(length > 1.0e-20) || !std::isfinite(length)) {
            const auto& normal = generated_normals[vertex];
            length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
            if (!(length > 1.0e-20) || !std::isfinite(length)) return false;
            for (size_t axis = 0; axis < 3; ++axis) {
                fixed_normals[vertex * 3 + axis] = float(normal[axis] / length);
            }
        }
    }

    std::vector<std::array<float, 4>> corner_frames(indices.size());
    mikktspace_detail::Input input{&positions, &fixed_normals, &uvs, &indices, &corner_frames};
    SMikkTSpaceInterface interface{};
    interface.m_getNumFaces = mikktspace_detail::face_count;
    interface.m_getNumVerticesOfFace = mikktspace_detail::vertices_per_face;
    interface.m_getPosition = mikktspace_detail::get_position;
    interface.m_getNormal = mikktspace_detail::get_normal;
    interface.m_getTexCoord = mikktspace_detail::get_uv;
    interface.m_setTSpaceBasic = mikktspace_detail::set_frame;
    SMikkTSpaceContext context{&interface, &input};
    if (!genTangSpaceDefault(&context)) return false;

    MikkGeometry result;
    result.indices.reserve(indices.size());
    result.source_vertices.reserve(std::min(indices.size(), vertex_count * 2));
    result.positions.reserve(std::min(indices.size(), vertex_count * 2) * 3);
    result.normals.reserve(std::min(indices.size(), vertex_count * 2) * 3);
    result.uvs.reserve(std::min(indices.size(), vertex_count * 2) * 2);
    result.tangents.reserve(std::min(indices.size(), vertex_count * 2) * 4);
    std::map<mikktspace_detail::VertexKey, uint32_t> vertices;
    for (size_t corner = 0; corner < indices.size(); ++corner) {
        const auto& frame = corner_frames[corner];
        const double tangent_length = std::sqrt(double(frame[0]) * frame[0] +
            double(frame[1]) * frame[1] + double(frame[2]) * frame[2]);
        if (!std::isfinite(tangent_length) || std::abs(tangent_length - 1.0) > 0.02 ||
            !std::isfinite(frame[3]) || std::abs(std::abs(frame[3]) - 1.0f) > 1.0e-4f) return false;
        const uint32_t source = indices[corner];
        const mikktspace_detail::VertexKey key{source, {mikktspace_detail::float_bits(frame[0]),
            mikktspace_detail::float_bits(frame[1]), mikktspace_detail::float_bits(frame[2]),
            mikktspace_detail::float_bits(frame[3])}};
        auto found = vertices.find(key);
        if (found == vertices.end()) {
            if (result.source_vertices.size() >= std::numeric_limits<uint32_t>::max()) return false;
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
