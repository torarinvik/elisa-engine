#pragma once
// meshoptimizer at the unverified boundary: the cooked package's index buffer
// is analyzed and cache-optimized before it is uploaded. The measured evidence
// is the average cache miss ratio before and after, and the run fails if
// optimization makes it worse, so the library earns its place rather than being
// linked for its own sake.
#include "probe_support.h"
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

} // namespace probe
