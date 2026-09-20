#pragma once
// meshoptimizer at the unverified boundary: the cooked package's index buffer
// is analyzed and cache-optimized before it is uploaded. The measured evidence
// is the average cache miss ratio before and after, and the run fails if
// optimization makes it worse, so the library earns its place rather than being
// linked for its own sake.
#include "probe_core.h"
#include "meshoptimizer.h"

#include <cstdio>
#include <vector>

namespace probe {

inline bool optimize_vertex_cache(std::vector<uint32_t>& indices, size_t vertex_count) {
    if (indices.empty() || vertex_count == 0) {
        return true;
    }
    const size_t index_count = indices.size();
    const meshopt_VertexCacheStatistics before =
        meshopt_analyzeVertexCache(indices.data(), index_count, vertex_count, 16, 0, 0);
    std::vector<uint32_t> optimized(index_count);
    meshopt_optimizeVertexCache(optimized.data(), indices.data(), index_count, vertex_count);
    indices.swap(optimized);
    const meshopt_VertexCacheStatistics after =
        meshopt_analyzeVertexCache(indices.data(), index_count, vertex_count, 16, 0, 0);
    std::fprintf(stdout, "meshoptimizer: indices=%u vertices=%u acmr=%.4f->%.4f\n",
        (unsigned)index_count, (unsigned)vertex_count, before.acmr, after.acmr);
    return check(after.acmr <= before.acmr + 0.0001, "meshoptimizer does not worsen the vertex cache");
}

inline bool simplify_lod(const std::vector<float>& positions,
    const std::vector<uint32_t>& source_indices, size_t target_index_count,
    float target_error, std::vector<uint32_t>& output, float& result_error) {
    if (positions.empty() || positions.size() % 3 != 0 || source_indices.size() < 3 ||
        source_indices.size() % 3 != 0 || target_index_count < 3 ||
        target_index_count >= source_indices.size() || target_error < 0.0f) return false;
    for (uint32_t index : source_indices) {
        if (index >= positions.size() / 3) return false;
    }
    output.resize(source_indices.size());
    result_error = 0.0f;
    const size_t simplified = meshopt_simplify(
        output.data(), source_indices.data(), source_indices.size(), positions.data(),
        positions.size() / 3, sizeof(float) * 3, target_index_count, target_error,
        meshopt_SimplifyPermissive,
        &result_error);
    if (simplified < 3 || simplified > target_index_count || simplified % 3 != 0) {
        output.clear();
        return false;
    }
    output.resize(simplified);
    std::fprintf(stdout, "meshoptimizer LOD: source_indices=%u lod_indices=%u error=%.6f\n",
        (unsigned)source_indices.size(), (unsigned)output.size(), result_error);
    return true;
}

} // namespace probe
