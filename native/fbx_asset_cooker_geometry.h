#pragma once

// FBX cooker geometry optimization, kept separate from package serialization.
#include "fbx_asset_import_types.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

namespace elisa::assets::cooker {

inline constexpr float MAX_SIMPLIFICATION_ERROR = 0.03f;

bool simplify_geometry(elisa::assets::FbxMeshData& mesh, size_t max_triangles) {
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
            target_indices, MAX_SIMPLIFICATION_ERROR, meshopt_SimplifyLockBorder, &result_error);
        if (simplified_count == 0 || simplified_count % 3 != 0 || simplified_count > target_indices ||
            !std::isfinite(result_error) || result_error > MAX_SIMPLIFICATION_ERROR) {
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

bool optimize_vertex_cache(elisa::assets::FbxMeshData& mesh) {
    const size_t vertex_count = mesh.positions.size() / 3;
    if (vertex_count == 0 || mesh.positions.size() % 3 != 0 || mesh.indices.empty() ||
        mesh.indices.size() % 3 != 0) {
        std::fprintf(stderr, "vertex-cache optimization requires indexed triangles\n");
        return false;
    }
    for (uint32_t index : mesh.indices) {
        if (index >= vertex_count) {
            std::fprintf(stderr, "vertex-cache optimization found an out-of-range index\n");
            return false;
        }
    }
    const meshopt_VertexCacheStatistics before = meshopt_analyzeVertexCache(
        mesh.indices.data(), mesh.indices.size(), vertex_count, 16, 0, 0);
    std::vector<uint32_t> optimized(mesh.indices.size());
    if (mesh.subsets.empty()) mesh.subsets.push_back({0, uint32_t(mesh.indices.size()), 0});
    for (const auto& subset : mesh.subsets) {
        if (subset.index_start > mesh.indices.size() || subset.index_count > mesh.indices.size() - subset.index_start ||
            subset.index_count % 3 != 0) {
            std::fprintf(stderr, "vertex-cache optimization found an invalid material subset range\n");
            return false;
        }
        meshopt_optimizeVertexCache(optimized.data() + subset.index_start,
            mesh.indices.data() + subset.index_start, subset.index_count, vertex_count);
    }
    const meshopt_VertexCacheStatistics after = meshopt_analyzeVertexCache(
        optimized.data(), optimized.size(), vertex_count, 16, 0, 0);
    if (!std::isfinite(before.acmr) || !std::isfinite(after.acmr)) {
        std::fprintf(stderr, "vertex-cache analysis returned a non-finite miss ratio\n");
        return false;
    }
    const bool improved = after.acmr + 0.0001f < before.acmr;
    if (after.acmr <= before.acmr + 0.0001f && improved) mesh.indices.swap(optimized);
    std::printf("meshoptimizer vertex cache: ACMR %.4f -> %.4f (candidate %.4f)\n",
        before.acmr, improved ? after.acmr : before.acmr, after.acmr);
    return true;
}

template <typename T>
bool remap_vertex_stream(const std::vector<T>& stream, size_t vertex_count, size_t remapped_count,
    const unsigned int* remap, std::vector<T>& output) {
    if (vertex_count == 0 || stream.size() % vertex_count != 0) return false;
    const size_t elements_per_vertex = stream.size() / vertex_count;
    if (elements_per_vertex == 0 || elements_per_vertex > 256 / sizeof(T)) return false;
    output.resize(remapped_count * elements_per_vertex);
    meshopt_remapVertexBuffer(output.data(), stream.data(), vertex_count,
        elements_per_vertex * sizeof(T), remap);
    const size_t bytes_per_vertex = elements_per_vertex * sizeof(T);
    for (size_t source = 0; source < vertex_count; ++source) {
        const unsigned int destination = remap[source];
        if (destination == ~0u) continue;
        if (destination >= remapped_count || std::memcmp(
            output.data() + size_t(destination) * elements_per_vertex,
            stream.data() + source * elements_per_vertex, bytes_per_vertex) != 0) return false;
    }
    return true;
}

bool optimize_vertex_fetch(elisa::assets::FbxMeshData& mesh) {
    const size_t vertex_count = mesh.positions.size() / 3;
    const bool has_skin_indices = !mesh.skin_indices.empty();
    const bool has_skin_weights = !mesh.skin_weights.empty();
    if (vertex_count == 0 || mesh.positions.size() != vertex_count * 3 ||
        mesh.normals.size() != vertex_count * 3 || mesh.uvs.size() != vertex_count * 2 ||
        mesh.tangents.size() != vertex_count * 4 || mesh.indices.empty() ||
        mesh.indices.size() % 3 != 0 || has_skin_indices != has_skin_weights ||
        (has_skin_indices && (mesh.skin_indices.size() != vertex_count * 4 ||
            mesh.skin_weights.size() != vertex_count * 4))) {
        std::fprintf(stderr, "vertex-fetch optimization requires matching geometry and skin streams\n");
        return false;
    }
    for (uint32_t index : mesh.indices) {
        if (index >= vertex_count) {
            std::fprintf(stderr, "vertex-fetch optimization found an out-of-range index\n");
            return false;
        }
    }

    const size_t vertex_stride = sizeof(float) * (3 + 3 + 2 + 4 + (has_skin_weights ? 4 : 0)) +
        sizeof(uint32_t) * (has_skin_indices ? 4 : 0);
    const meshopt_VertexFetchStatistics before = meshopt_analyzeVertexFetch(
        mesh.indices.data(), mesh.indices.size(), vertex_count, vertex_stride);
    std::vector<unsigned int> remap(vertex_count);
    const size_t remapped_count = meshopt_optimizeVertexFetchRemap(
        remap.data(), mesh.indices.data(), mesh.indices.size(), vertex_count);
    if (remapped_count == 0 || remapped_count > vertex_count) {
        std::fprintf(stderr, "vertex-fetch remapper returned an invalid vertex count\n");
        return false;
    }
    std::vector<uint32_t> remapped_indices(mesh.indices.size());
    meshopt_remapIndexBuffer(remapped_indices.data(), mesh.indices.data(), mesh.indices.size(), remap.data());
    const meshopt_VertexFetchStatistics after = meshopt_analyzeVertexFetch(
        remapped_indices.data(), remapped_indices.size(), remapped_count, vertex_stride);
    const bool improved_or_compacted = after.bytes_fetched <= before.bytes_fetched || remapped_count < vertex_count;
    if (!improved_or_compacted) {
        std::printf("meshoptimizer vertex fetch: bytes fetched %u -> %u (candidate %u), vertices %zu -> %zu\n",
            before.bytes_fetched, before.bytes_fetched, after.bytes_fetched, vertex_count, vertex_count);
        return true;
    }

    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> tangents;
    std::vector<uint32_t> skin_indices;
    std::vector<float> skin_weights;
    const bool streams_valid = remap_vertex_stream(mesh.positions, vertex_count, remapped_count, remap.data(), positions) &&
        remap_vertex_stream(mesh.normals, vertex_count, remapped_count, remap.data(), normals) &&
        remap_vertex_stream(mesh.uvs, vertex_count, remapped_count, remap.data(), uvs) &&
        remap_vertex_stream(mesh.tangents, vertex_count, remapped_count, remap.data(), tangents) &&
        (!has_skin_indices || (remap_vertex_stream(mesh.skin_indices, vertex_count, remapped_count, remap.data(), skin_indices) &&
            remap_vertex_stream(mesh.skin_weights, vertex_count, remapped_count, remap.data(), skin_weights)));
    const bool indices_valid = std::all_of(remapped_indices.begin(), remapped_indices.end(),
        [remapped_count](uint32_t index) { return index < remapped_count; });
    if (!streams_valid || !indices_valid) {
        std::fprintf(stderr, "vertex-fetch remap could not validate every stream and index\n");
        return false;
    }
    mesh.positions.swap(positions);
    mesh.normals.swap(normals);
    mesh.uvs.swap(uvs);
    mesh.tangents.swap(tangents);
    if (has_skin_indices) {
        mesh.skin_indices.swap(skin_indices);
        mesh.skin_weights.swap(skin_weights);
    }
    mesh.indices.swap(remapped_indices);
    std::printf("meshoptimizer vertex fetch: bytes fetched %u -> %u (candidate %u), vertices %zu -> %zu\n",
        before.bytes_fetched, after.bytes_fetched, after.bytes_fetched, vertex_count, remapped_count);
    return true;
}

} // namespace elisa::assets::cooker
