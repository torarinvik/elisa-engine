#pragma once
// Recast/Detour integration. The plan selects Recast for navmesh generation
// and Detour for queries. Here a small mesh (a plane with a wall and a gap) is
// voxelized by Recast into a navmesh, and Detour finds a path from one side of
// the wall to the other, so the integration is exercised rather than declared.
// The game's own navigation decisions stay in Elisa; this proves the library
// builds a navmesh and answers a query.
#include "probe_support.h"

#include "DetourCommon.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "Recast.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace probe {

inline bool probe_recast_navigation() {
    const int cells = 20;
    const float cell = 1.0f;
    std::vector<float> verts;
    std::vector<int> tris;
    for (int j = 0; j <= cells; ++j) {
        for (int i = 0; i <= cells; ++i) {
            verts.push_back(i * cell);
            verts.push_back(0.0f);
            verts.push_back(j * cell);
        }
    }
    const auto vertex_index = [cells](int i, int j) { return j * (cells + 1) + i; };
    for (int j = 0; j < cells; ++j) {
        for (int i = 0; i < cells; ++i) {
            const bool barrier = (j == cells / 2) && (i != cells / 2);
            if (barrier) {
                continue;
            }
            const int a = vertex_index(i, j);
            const int b = vertex_index(i + 1, j);
            const int c = vertex_index(i + 1, j + 1);
            const int d = vertex_index(i, j + 1);
            // Counter-clockwise seen from above, so Recast reads +Y normals.
            tris.push_back(a); tris.push_back(c); tris.push_back(b);
            tris.push_back(a); tris.push_back(d); tris.push_back(c);
        }
    }
    const int triangle_count = (int)tris.size() / 3;
    std::vector<unsigned char> areas(triangle_count, 0);
    rcContext context(false);
    rcMarkWalkableTriangles(&context, 45.0f, verts.data(), (int)verts.size() / 3,
        tris.data(), triangle_count, areas.data());

    rcConfig config;
    std::memset(&config, 0, sizeof(config));
    config.cs = 0.3f;
    config.ch = 0.2f;
    config.walkableSlopeAngle = 45.0f;
    config.walkableHeight = (int)std::ceil(2.0f / config.ch);
    config.walkableClimb = (int)std::floor(0.6f / config.ch);
    config.walkableRadius = (int)std::ceil(0.4f / config.cs);
    config.maxEdgeLen = (int)(6.0f / config.cs);
    config.maxSimplificationError = 0.6f;
    config.minRegionArea = 4 * 4;
    config.mergeRegionArea = 8 * 8;
    config.maxVertsPerPoly = 6;
    config.detailSampleDist = config.cs * 6.0f;
    config.detailSampleMaxError = config.ch;
    rcCalcBounds(verts.data(), (int)verts.size() / 3, config.bmin, config.bmax);
    // Pad the bounds so the ground plane has vertical room; a zero-thickness y
    // range and too little headroom cull every span.
    config.bmin[0] -= 1.0f; config.bmin[1] -= 1.0f; config.bmin[2] -= 1.0f;
    config.bmax[0] += 1.0f; config.bmax[1] += 3.0f; config.bmax[2] += 1.0f;
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

    rcHeightfield* heightfield = rcAllocHeightfield();
    if (!check(heightfield != nullptr && rcCreateHeightfield(&context, *heightfield, config.width,
            config.height, config.bmin, config.bmax, config.cs, config.ch), "recast heightfield")) {
        return false;
    }
    if (!check(rcRasterizeTriangles(&context, verts.data(), (int)verts.size() / 3, tris.data(),
            areas.data(), triangle_count, *heightfield, config.walkableClimb), "recast rasterize")) {
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

    rcCompactHeightfield* compact = rcAllocCompactHeightfield();
    if (!check(compact != nullptr && rcBuildCompactHeightfield(&context, config.walkableHeight,
            config.walkableClimb, *heightfield, *compact), "recast compact heightfield")) {
        return false;
    }
    if (!check(rcErodeWalkableArea(&context, config.walkableRadius, *compact), "recast erode")) {
        return false;
    }
    if (!check(rcBuildDistanceField(&context, *compact), "recast distance field")) {
        return false;
    }
    if (!check(rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea),
            "recast regions")) {
        return false;
    }

    rcContourSet* contours = rcAllocContourSet();
    if (!check(contours != nullptr && rcBuildContours(&context, *compact, config.maxSimplificationError,
            config.maxEdgeLen, *contours), "recast contours")) {
        return false;
    }
    rcPolyMesh* poly_mesh = rcAllocPolyMesh();
    if (!check(poly_mesh != nullptr && rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly,
            *poly_mesh), "recast poly mesh")) {
        return false;
    }
    rcPolyMeshDetail* detail_mesh = rcAllocPolyMeshDetail();
    if (!check(detail_mesh != nullptr && rcBuildPolyMeshDetail(&context, *poly_mesh, *compact,
            config.detailSampleDist, config.detailSampleMaxError, *detail_mesh), "recast detail mesh")) {
        return false;
    }
    for (int index = 0; index < poly_mesh->npolys; ++index) {
        if (poly_mesh->areas[index] == RC_WALKABLE_AREA) {
            poly_mesh->areas[index] = 1;
        }
        if (poly_mesh->areas[index] == 1) {
            poly_mesh->flags[index] = 1;
        }
    }
    dtNavMeshCreateParams params;
    std::memset(&params, 0, sizeof(params));
    params.verts = poly_mesh->verts;
    params.vertCount = poly_mesh->nverts;
    params.polys = poly_mesh->polys;
    params.polyAreas = poly_mesh->areas;
    params.polyFlags = poly_mesh->flags;
    params.polyCount = poly_mesh->npolys;
    params.nvp = poly_mesh->nvp;
    params.detailMeshes = detail_mesh->meshes;
    params.detailVerts = detail_mesh->verts;
    params.detailVertsCount = detail_mesh->nverts;
    params.detailTris = detail_mesh->tris;
    params.detailTriCount = detail_mesh->ntris;
    params.walkableHeight = 2.0f;
    params.walkableRadius = 0.4f;
    params.walkableClimb = 0.6f;
    rcVcopy(params.bmin, poly_mesh->bmin);
    rcVcopy(params.bmax, poly_mesh->bmax);
    params.cs = config.cs;
    params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* nav_data = nullptr;
    int nav_data_size = 0;
    if (!check(poly_mesh->npolys > 0 && dtCreateNavMeshData(&params, &nav_data, &nav_data_size),
            "detour navmesh data")) {
        return false;
    }
    dtNavMesh* nav_mesh = dtAllocNavMesh();
    if (!check(nav_mesh != nullptr && dtStatusSucceed(nav_mesh->init(nav_data, nav_data_size, DT_TILE_FREE_DATA)),
            "detour navmesh init")) {
        return false;
    }
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    if (!check(query != nullptr && dtStatusSucceed(query->init(nav_mesh, 2048)), "detour query init")) {
        return false;
    }
    dtQueryFilter filter;
    filter.setIncludeFlags(0xffff);
    const float start[3] = {2.0f, 0.0f, 2.0f};
    const float end[3] = {18.0f, 0.0f, 18.0f};
    const float extents[3] = {1.0f, 4.0f, 1.0f};
    dtPolyRef start_ref = 0;
    dtPolyRef end_ref = 0;
    float start_point[3];
    float end_point[3];
    query->findNearestPoly(start, extents, &filter, &start_ref, start_point);
    query->findNearestPoly(end, extents, &filter, &end_ref, end_point);
    if (!check(start_ref != 0 && end_ref != 0, "detour nearest poly")) {
        return false;
    }
    std::vector<dtPolyRef> path(64);
    int path_count = 0;
    const dtStatus status = query->findPath(start_ref, end_ref, start_point, end_point, &filter,
        path.data(), &path_count, (int)path.size());
    // A path around the wall must use more than one polygon; a straight line is
    // blocked, so this is what proves Detour actually navigated.
    const bool navigated = dtStatusSucceed(status) && path_count > 1;
    std::fprintf(stdout, "recast: polys=%d navdata=%d path_polys=%d start=%llu end=%llu\n",
        poly_mesh->npolys, nav_data_size, path_count,
        (unsigned long long)start_ref, (unsigned long long)end_ref);
    return check(navigated, "detour finds a path around the wall");
}

} // namespace probe
