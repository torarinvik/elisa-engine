#pragma once

// Validate that per-node FBX placement ranges exactly cover the cooked streams.
#include "fbx_asset_import.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace elisa::assets::cooker_detail {

bool finalize_mesh_placements(elisa::assets::FbxMeshData& mesh) {
    if (mesh.mesh_placements.empty()) return true;
    if (mesh.mesh_placements.size() != mesh.source_mesh_count ||
        mesh.mesh_placements.size() > 256) {
        std::fprintf(stderr, "FBX placement records do not match the source mesh count\n");
        return false;
    }
    uint64_t next_vertex = 0;
    uint64_t next_index = 0;
    uint64_t next_subset = 0;
    for (size_t placement_index = 0; placement_index < mesh.mesh_placements.size(); ++placement_index) {
        auto& placement = mesh.mesh_placements[placement_index];
        if (placement.mesh != placement_index || placement.node >= 256 ||
            placement.subset_start != next_subset || placement.subset_count == 0 ||
            uint64_t(placement.subset_start) + placement.subset_count > mesh.subsets.size()) {
            std::fprintf(stderr, "FBX cooker received an invalid source mesh placement\n");
            return false;
        }
        const auto& first_subset = mesh.subsets[placement.subset_start];
        if (first_subset.index_start != next_index) {
            std::fprintf(stderr, "FBX placement subsets do not partition the index stream\n");
            return false;
        }
        placement.index_start = first_subset.index_start;
        uint64_t index_count = 0;
        for (size_t subset_index = placement.subset_start;
            subset_index < size_t(placement.subset_start) + placement.subset_count; ++subset_index) {
            const auto& subset = mesh.subsets[subset_index];
            if (subset.index_start != next_index + index_count || subset.index_count == 0 ||
                uint64_t(subset.index_start) + subset.index_count > mesh.indices.size()) {
                std::fprintf(stderr, "FBX placement contains an invalid material subset range\n");
                return false;
            }
            index_count += subset.index_count;
        }
        if (index_count == 0 || index_count > std::numeric_limits<uint32_t>::max() ||
            uint64_t(placement.index_start) + index_count > mesh.indices.size()) {
            std::fprintf(stderr, "FBX placement index range exceeds the cooked stream\n");
            return false;
        }
        placement.index_count = uint32_t(index_count);
        uint32_t minimum = std::numeric_limits<uint32_t>::max();
        uint32_t maximum = 0;
        for (size_t offset = placement.index_start;
            offset < size_t(placement.index_start) + placement.index_count; ++offset) {
            const uint32_t index = mesh.indices[offset];
            if (index >= mesh.positions.size() / 3) {
                std::fprintf(stderr, "FBX placement references an out-of-range vertex\n");
                return false;
            }
            minimum = std::min(minimum, index);
            maximum = std::max(maximum, index);
        }
        if (minimum != next_vertex || uint64_t(maximum) + 1 < minimum ||
            uint64_t(maximum) + 1 - minimum > std::numeric_limits<uint32_t>::max()) {
            std::fprintf(stderr, "FBX cooker could not retain contiguous per-mesh vertex ranges\n");
            return false;
        }
        placement.vertex_start = minimum;
        placement.vertex_count = uint32_t(uint64_t(maximum) + 1 - minimum);
        next_vertex += placement.vertex_count;
        next_index += placement.index_count;
        next_subset += placement.subset_count;
    }
    if (next_vertex != mesh.positions.size() / 3 || next_index != mesh.indices.size() ||
        next_subset != mesh.subsets.size()) {
        std::fprintf(stderr, "FBX placement ranges do not cover the cooked geometry streams\n");
        return false;
    }
    return true;
}

} // namespace elisa::assets::cooker_detail
