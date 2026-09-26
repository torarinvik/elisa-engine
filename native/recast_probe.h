#pragma once

// Native navigation acceptance fixture. The reusable ownership and query
// boundary lives in navmesh_service.h; this file only supplies a deterministic
// wall-and-gap mesh and adversarial calls for the native gate.
#include "navmesh_service.h"
#include "probe_core.h"

#include <cstdio>
#include <limits>
#include <string>
#include <vector>

namespace probe {

inline bool probe_recast_navigation() {
    constexpr int cells = 20;
    constexpr float cell = 1.0f;
    std::vector<float> vertices;
    std::vector<int> indices;
    vertices.reserve((cells + 1) * (cells + 1) * 3);
    for (int z = 0; z <= cells; ++z) {
        for (int x = 0; x <= cells; ++x) {
            vertices.push_back(x * cell);
            vertices.push_back(0.0f);
            vertices.push_back(z * cell);
        }
    }
    const auto vertex_index = [cells](int x, int z) { return z * (cells + 1) + x; };
    for (int z = 0; z < cells; ++z) {
        for (int x = 0; x < cells; ++x) {
            if (z == cells / 2 && x != cells / 2) continue;
            const int a = vertex_index(x, z);
            const int b = vertex_index(x + 1, z);
            const int c = vertex_index(x + 1, z + 1);
            const int d = vertex_index(x, z + 1);
            indices.insert(indices.end(), {a, c, b, a, d, c});
        }
    }
    probe::nav::BakeInput input;
    input.vertices = vertices.data();
    input.vertex_count = static_cast<int>(vertices.size() / 3);
    input.indices = indices.data();
    input.triangle_count = static_cast<int>(indices.size() / 3);
    input.source_generation = 17;
    probe::nav::NavMeshArtifact artifact;
    std::string error;
    if (!check(probe::nav::bake(input, artifact, error), "recast bake service")) {
        std::fprintf(stderr, "recast bake diagnostic: %s\n", error.c_str());
        return false;
    }
    const auto& metadata = artifact.metadata();
    if (!check(metadata.source_generation == 17 && metadata.polygon_count > 0 &&
        metadata.nav_data_size > 0 && metadata.width > 0 && metadata.height > 0 &&
        artifact.serialized_tile().size() == static_cast<size_t>(metadata.nav_data_size),
        "recast bake metadata")) {
        return false;
    }
    const float start[3] = {2.0f, 0.0f, 2.0f};
    const float end[3] = {18.0f, 0.0f, 18.0f};
    const float extents[3] = {1.0f, 4.0f, 1.0f};
    probe::nav::PathResult path;
    const auto status = artifact.query_path(start, end, extents, path);
    if (!check(status == probe::nav::QueryStatus::Success && path.polygon_count > 1 &&
        path.point_count > 1, "detour finds a path around the wall")) {
        return false;
    }
    const int successful_polys = path.polygon_count;
    const int successful_points = path.point_count;
    const auto nearest = artifact.nearest_point(start, extents);
    if (!check(nearest.status == probe::nav::QueryStatus::Success && nearest.polygon != 0,
        "detour nearest point")) {
        return false;
    }
    const float blocked_end[3] = {2.0f, 0.0f, 18.0f};
    const auto ray = artifact.raycast(start, blocked_end, extents);
    if (!check(ray.status == probe::nav::QueryStatus::Success && ray.hit_wall &&
        ray.fraction >= 0.0f && ray.fraction < 1.0f, "detour raycast wall")) {
        return false;
    }
    if (!check(artifact.query_path(nullptr, end, extents) ==
        probe::nav::QueryStatus::InvalidInput, "detour rejects null query input")) {
        return false;
    }
    const float bad_extents[3] = {0.0f, 1.0f, 1.0f};
    if (!check(artifact.query_path(start, end, bad_extents) ==
        probe::nav::QueryStatus::InvalidInput, "detour rejects zero query extent")) {
        return false;
    }
    if (!check(artifact.query_path(start, end, extents, path, 0) ==
        probe::nav::QueryStatus::NoPath, "detour reports filtered query")) {
        return false;
    }
    std::vector<unsigned char> annotated_areas(input.triangle_count, 7);
    probe::nav::BakeInput annotated = input;
    annotated.areas = annotated_areas.data();
    probe::nav::NavMeshArtifact annotated_artifact;
    probe::nav::PathResult annotated_path;
    if (!check(probe::nav::bake(annotated, annotated_artifact, error) &&
        annotated_artifact.query_path(start, end, extents, annotated_path, 1) ==
            probe::nav::QueryStatus::Success,
        "detour preserves custom area annotations as traversable polygons")) {
        return false;
    }
    const auto annotated_nearest = annotated_artifact.nearest_point(start, extents, 1);
    if (!check(annotated_nearest.status == probe::nav::QueryStatus::Success &&
        annotated_nearest.area == 7,
        "detour nearest-point query reports the preserved custom area id")) {
        return false;
    }
    std::vector<unsigned char> invalid_areas(input.triangle_count, DT_MAX_AREAS);
    probe::nav::NavMeshArtifact rejected;
    probe::nav::BakeInput invalid_area_input = input;
    invalid_area_input.areas = invalid_areas.data();
    if (!check(!probe::nav::bake(invalid_area_input, rejected, error),
        "recast rejects areas outside Detour's filter range")) {
        return false;
    }
    probe::nav::BakeInput invalid_profile = input;
    invalid_profile.agent.slope = std::numeric_limits<float>::infinity();
    if (!check(!probe::nav::bake(invalid_profile, rejected, error),
        "recast rejects non-finite agent profiles")) {
        return false;
    }
    const float large_vertices[12] = {
        0.0f, 0.0f, 0.0f, 2000.0f, 0.0f, 0.0f,
        2000.0f, 0.0f, 2000.0f, 0.0f, 0.0f, 2000.0f,
    };
    const int large_indices[6] = {0, 2, 1, 0, 3, 2};
    probe::nav::BakeInput oversized = input;
    oversized.vertices = large_vertices;
    oversized.vertex_count = 4;
    oversized.indices = large_indices;
    oversized.triangle_count = 2;
    oversized.cell_size = 0.1f;
    if (!check(!probe::nav::bake(oversized, rejected, error),
        "recast rejects excessive grid allocation before rasterizing")) {
        return false;
    }
    std::vector<int> bad_indices = indices;
    bad_indices[0] = input.vertex_count + 1;
    probe::nav::BakeInput malformed = input;
    malformed.indices = bad_indices.data();
    if (!check(!probe::nav::bake(malformed, rejected, error), "recast rejects bad indices")) {
        return false;
    }
    probe::nav::NavMeshTileStore store;
    const auto tile = store.publish(input, error);
    probe::nav::PathResult tile_path;
    if (!check(store.live(tile) && store.query_path(tile, start, end, extents, tile_path) == probe::nav::QueryStatus::Success,
        "detour tile store queries a live generation") ||
        !check(store.unload(tile) && !store.live(tile) &&
            store.query_path(tile, start, end, extents, tile_path) == probe::nav::QueryStatus::InvalidInput,
            "detour tile store rejects stale generation")) return false;
    std::fprintf(stdout, "recast: polys=%d navdata=%d path_polys=%d path_points=%d source=%u\n",
        metadata.polygon_count, metadata.nav_data_size, successful_polys, successful_points,
        metadata.source_generation);
    return true;
}

} // namespace probe
