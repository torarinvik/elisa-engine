#include "mikktspace_geometry.h"

#include <cstdio>
#include <vector>

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
    std::vector<float> missing_normals(normals.size(), 0.0f);
    elisa::assets::MikkGeometry recovered;
    if (!elisa::assets::generate_mikktspace_geometry(positions, missing_normals, uvs,
        indices, recovered) || recovered.normals.empty() || recovered.normals[2] < 0.99f) {
        std::fprintf(stderr, "MikkTSpace failed to recover missing source normals\n");
        return 1;
    }
    std::vector<uint32_t> invalid_indices = indices;
    invalid_indices[0] = uint32_t(positions.size());
    if (elisa::assets::generate_mikktspace_geometry(positions, normals, uvs,
        invalid_indices, recovered)) {
        std::fprintf(stderr, "MikkTSpace accepted an out-of-range index\n");
        return 1;
    }
    std::printf("MikkTSpace mirrored seam, normal fallback, and deterministic split passed (%zu -> %zu vertices)\n",
        positions.size() / 3, first.source_vertices.size());
    return 0;
}
