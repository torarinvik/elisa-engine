#include "mikktspace_geometry.h"

#include <cstdio>
#include <cmath>
#include <vector>

static bool valid_frames(const elisa::assets::MikkGeometry& geometry) {
    for (size_t vertex = 0; vertex < geometry.source_vertices.size(); ++vertex) {
        const float* normal = geometry.normals.data() + vertex * 3;
        const float* tangent = geometry.tangents.data() + vertex * 4;
        const float length = std::sqrt(tangent[0] * tangent[0] + tangent[1] * tangent[1] +
            tangent[2] * tangent[2]);
        const float dot = tangent[0] * normal[0] + tangent[1] * normal[1] + tangent[2] * normal[2];
        if (!std::isfinite(length) || std::abs(length - 1.0f) > 0.02f ||
            !std::isfinite(dot) || std::abs(dot) > 0.02f || std::abs(std::abs(tangent[3]) - 1.0f) > 1.0e-4f) {
            return false;
        }
    }
    return true;
}

int main() {
    const std::vector<float> positions = {0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 1, 0};
    const std::vector<float> normals = {0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::vector<float> uvs = {0, 0, 1, 0, 0, 1, 0, 0};
    const std::vector<uint32_t> indices = {0, 1, 2, 1, 3, 2};
    elisa::assets::MikkGeometry first;
    elisa::assets::MikkGeometry repeat;
    if (!elisa::assets::generate_mikktspace_geometry(positions, normals, uvs, indices, first) ||
        !elisa::assets::generate_mikktspace_geometry(positions, normals, uvs, indices, repeat)) {
        std::fprintf(stderr, "MikkTSpace failed to generate frames for the mirrored-UV fixture\n");
        return 1;
    }
    if (first.source_vertices.size() <= positions.size() / 3 || first.indices.size() != indices.size() ||
        first.source_vertices != repeat.source_vertices || first.positions != repeat.positions ||
        first.normals != repeat.normals || first.uvs != repeat.uvs ||
        first.tangents != repeat.tangents || first.indices != repeat.indices) {
        std::fprintf(stderr, "MikkTSpace did not split seams deterministically\n");
        return 1;
    }
    const float first_sign = first.tangents[first.indices[0] * 4 + 3];
    const float mirrored_sign = first.tangents[first.indices[3] * 4 + 3];
    if (first_sign != 1.0f || mirrored_sign != -1.0f) {
        std::fprintf(stderr, "mirrored UV orientations have wrong signs: %.1f %.1f\n",
            first_sign, mirrored_sign);
        return 1;
    }
    elisa::assets::MikkGeometry bounded;
    if (elisa::assets::generate_mikktspace_geometry(positions, normals, uvs,
        indices, bounded, positions.size() / 3)) {
        std::fprintf(stderr, "MikkTSpace exceeded the caller's output vertex bound\n");
        return 1;
    }
    std::vector<float> missing_normals(normals.size(), 0.0f);
    elisa::assets::MikkGeometry recovered;
    if (!elisa::assets::generate_mikktspace_geometry(positions, missing_normals, uvs,
        indices, recovered) || recovered.normals.empty() || recovered.normals[2] < 0.99f) {
        std::fprintf(stderr, "MikkTSpace failed to recover missing source normals\n");
        return 1;
    }
    std::vector<float> unused_positions = positions;
    std::vector<float> unused_normals = normals;
    std::vector<float> unused_uvs = uvs;
    unused_positions.insert(unused_positions.end(), {10.0f, 20.0f, 30.0f});
    unused_normals.insert(unused_normals.end(), {0.0f, 0.0f, 0.0f});
    unused_uvs.insert(unused_uvs.end(), {0.0f, 0.0f});
    if (!elisa::assets::generate_mikktspace_geometry(unused_positions, unused_normals,
        unused_uvs, indices, recovered) || recovered.tangents.size() != first.tangents.size() ||
        !valid_frames(recovered)) {
        std::fprintf(stderr, "MikkTSpace failed to ignore an unreferenced control point\n");
        return 1;
    }
    const std::vector<float> triangle_positions = {0, 0, 0, 1, 0, 0, 0, 1, 0};
    const std::vector<float> triangle_normals = {0, 0, 1, 0, 0, 1, 0, 0, 1};
    const std::vector<float> degenerate_uvs = {0, 0, 0, 0, 0, 0};
    const std::vector<uint32_t> triangle_indices = {0, 1, 2};
    if (!elisa::assets::generate_mikktspace_geometry(triangle_positions, triangle_normals,
        degenerate_uvs, triangle_indices, recovered) || !valid_frames(recovered)) {
        std::fprintf(stderr, "MikkTSpace failed to provide a frame for a degenerate UV chart\n");
        return 1;
    }
    const std::vector<float> degenerate_positions(9, 0.0f);
    const std::vector<float> zero_normals(9, 0.0f);
    if (!elisa::assets::generate_mikktspace_geometry(degenerate_positions, zero_normals,
        degenerate_uvs, triangle_indices, recovered) || !valid_frames(recovered)) {
        std::fprintf(stderr, "MikkTSpace failed to bound a fully degenerate triangle frame\n");
        return 1;
    }
    std::vector<uint32_t> invalid_indices = indices;
    invalid_indices[0] = uint32_t(positions.size());
    if (elisa::assets::generate_mikktspace_geometry(positions, normals, uvs,
        invalid_indices, recovered)) {
        std::fprintf(stderr, "MikkTSpace accepted an out-of-range index\n");
        return 1;
    }
    std::printf("MikkTSpace mirrored seam, normal/UV fallbacks, and deterministic split passed (%zu -> %zu vertices)\n",
        positions.size() / 3, first.source_vertices.size());
    return 0;
}
