#pragma once

#include "fbx_asset_import_types.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace elisa::assets::detail {

inline constexpr float MAX_FBX_SIMPLIFICATION_ERROR = 0.03f;

inline bool simplify_fbx_geometry(elisa::assets::FbxMeshData& mesh, size_t max_triangles) {
    if (max_triangles == 0 || mesh.indices.size() / 3 <= max_triangles) return true;
    if (!mesh.skin_indices.empty()) {
        std::fprintf(stderr, "skinned FBX geometry cannot be simplified until bone influences are remapped with the mesh\n");
        return false;
    }
    const size_t original_triangles = mesh.indices.size() / 3;
    if (mesh.subsets.empty()) mesh.subsets.push_back({0, uint32_t(mesh.indices.size()), 0});
    if (max_triangles < mesh.subsets.size()) {
        std::fprintf(stderr, "triangle budget must retain at least one triangle per FBX material subset\n");
        return false;
    }
    std::vector<size_t> subset_triangles(mesh.subsets.size());
    for (size_t subset = 0; subset < mesh.subsets.size(); ++subset) {
        subset_triangles[subset] = mesh.subsets[subset].index_count / 3;
        if (subset_triangles[subset] == 0 || mesh.subsets[subset].index_count % 3 != 0) return false;
    }
    if (original_triangles < mesh.subsets.size()) return false;
    const size_t distributable = max_triangles - mesh.subsets.size();
    const size_t source_distributable = original_triangles - mesh.subsets.size();
    std::vector<size_t> targets(mesh.subsets.size(), 1);
    std::vector<long double> remainders(mesh.subsets.size(), 0.0L);
    size_t allocated = mesh.subsets.size();
    if (source_distributable != 0) {
        for (size_t subset = 0; subset < mesh.subsets.size(); ++subset) {
            const long double share = (static_cast<long double>(distributable) *
                static_cast<long double>(subset_triangles[subset] - 1)) /
                static_cast<long double>(source_distributable);
            const size_t whole = size_t(share);
            targets[subset] += whole;
            remainders[subset] = share - static_cast<long double>(whole);
            allocated += whole;
        }
        while (allocated < max_triangles) {
            size_t best = mesh.subsets.size();
            for (size_t subset = 0; subset < mesh.subsets.size(); ++subset) {
                if (targets[subset] >= subset_triangles[subset]) continue;
                if (best == mesh.subsets.size() || remainders[subset] > remainders[best]) best = subset;
            }
            if (best == mesh.subsets.size()) break;
            ++targets[best];
            remainders[best] = -1.0L;
            ++allocated;
        }
    }
    std::vector<uint32_t> simplified_indices;
    std::vector<elisa::assets::FbxMeshSubset> simplified_subsets;
    simplified_indices.reserve(max_triangles * 3);
    float maximum_error = 0.0f;
    for (size_t subset = 0; subset < mesh.subsets.size(); ++subset) {
        const auto& source = mesh.subsets[subset];
        std::vector<uint32_t> simplified(source.index_count);
        float result_error = 0.0f;
        const size_t target_indices = targets[subset] * 3;
        const size_t simplified_count = meshopt_simplify(simplified.data(),
            mesh.indices.data() + source.index_start, source.index_count,
            mesh.positions.data(), mesh.positions.size() / 3, sizeof(float) * 3,
            target_indices, MAX_FBX_SIMPLIFICATION_ERROR, meshopt_SimplifyLockBorder, &result_error);
        if (simplified_count == 0 || simplified_count % 3 != 0 || simplified_count > target_indices ||
            !std::isfinite(result_error) || result_error > MAX_FBX_SIMPLIFICATION_ERROR) {
            std::fprintf(stderr, "mesh simplification did not meet the requested triangle budget\n");
            return false;
        }
        maximum_error = std::max(maximum_error, result_error);
        simplified_subsets.push_back({uint32_t(simplified_indices.size()),
            uint32_t(simplified_count), source.material_slot});
        simplified_indices.insert(simplified_indices.end(), simplified.begin(),
            simplified.begin() + simplified_count);
    }
    const uint32_t unused = std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> remap(mesh.positions.size() / 3, unused);
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<uint32_t> indices;
    positions.reserve(simplified_indices.size() * 3);
    normals.reserve(simplified_indices.size() * 3);
    uvs.reserve(simplified_indices.size() * 2);
    indices.reserve(simplified_indices.size());
    for (uint32_t source_index : simplified_indices) {
        if (source_index >= remap.size()) {
            std::fprintf(stderr, "mesh simplifier returned an out-of-range index\n");
            return false;
        }
        if (remap[source_index] == unused) {
            remap[source_index] = uint32_t(positions.size() / 3);
            for (size_t axis = 0; axis < 3; ++axis) {
                positions.push_back(mesh.positions[source_index * 3 + axis]);
                normals.push_back(mesh.normals[source_index * 3 + axis]);
            }
            uvs.push_back(mesh.uvs[source_index * 2]);
            uvs.push_back(mesh.uvs[source_index * 2 + 1]);
        }
        indices.push_back(remap[source_index]);
    }
    mesh.positions.swap(positions);
    mesh.normals.swap(normals);
    mesh.uvs.swap(uvs);
    mesh.indices.swap(indices);
    mesh.subsets.swap(simplified_subsets);
    std::printf("simplified %zu -> %zu triangles across %zu material subsets (relative error <= %.5f, %zu vertices)\n",
        original_triangles, mesh.indices.size() / 3, mesh.subsets.size(), maximum_error, mesh.positions.size() / 3);
    return true;
}


} // namespace elisa::assets::detail
