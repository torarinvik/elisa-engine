#pragma once

// Recast bake and Detour query ownership. Elisa owns agent intent and movement;
// this adapter owns only bounded native mesh data and query scratch space.
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "Recast.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace probe::nav {

constexpr int MAX_VERTICES = 65535;
constexpr int MAX_TRIANGLES = 131072;
constexpr int MAX_POLYS = 32768;
constexpr int MAX_QUERY_NODES = 2048;
constexpr int MAX_PATH_POLYS = 256;
constexpr int MAX_STRAIGHT_POINTS = 256;
constexpr int MAX_GRID_DIMENSION = 16384;
constexpr uint64_t MAX_GRID_CELLS = 1024 * 1024;
constexpr double MAX_CONFIG_SCALE = 16384.0;

struct AgentProfile {
    float height = 2.0f;
    float radius = 0.4f;
    float climb = 0.6f;
    float slope = 45.0f;
};

struct BakeInput {
    const float* vertices = nullptr;
    int vertex_count = 0;
    const int* indices = nullptr;
    int triangle_count = 0;
    const unsigned char* areas = nullptr;
    AgentProfile agent{};
    float cell_size = 0.3f;
    float cell_height = 0.2f;
    uint32_t source_generation = 0;
};

struct BakeMetadata {
    uint32_t source_generation = 0;
    uint32_t settings_generation = 1;
    int polygon_count = 0;
    int nav_data_size = 0;
    int width = 0;
    int height = 0;
};

enum class QueryStatus {
    Success,
    Partial,
    InvalidInput,
    NoPath,
    Capacity,
};

struct PathResult {
    QueryStatus status = QueryStatus::InvalidInput;
    std::array<dtPolyRef, MAX_PATH_POLYS> polygons{};
    int polygon_count = 0;
    std::array<float, MAX_STRAIGHT_POINTS * 3> points{};
    int point_count = 0;
};

struct NearestResult {
    QueryStatus status = QueryStatus::InvalidInput;
    dtPolyRef polygon = 0;
    uint8_t area = 0;
    std::array<float, 3> point{};
};

struct RaycastResult {
    QueryStatus status = QueryStatus::InvalidInput;
    bool hit_wall = false;
    float fraction = 0.0f;
    std::array<float, 3> normal{};
};

class NavMeshArtifact {
public:
    NavMeshArtifact() = default;
    NavMeshArtifact(const NavMeshArtifact&) = delete;
    NavMeshArtifact& operator=(const NavMeshArtifact&) = delete;
    ~NavMeshArtifact() { reset(); }

    void reset() {
        if (query_ != nullptr) {
            dtFreeNavMeshQuery(query_);
            query_ = nullptr;
        }
        if (mesh_ != nullptr) {
            dtFreeNavMesh(mesh_);
            mesh_ = nullptr;
        }
        metadata_ = {};
        tile_data_.clear();
        ready_ = false;
    }

    bool ready() const { return ready_ && mesh_ != nullptr && query_ != nullptr; }
    const BakeMetadata& metadata() const { return metadata_; }
    const std::vector<unsigned char>& serialized_tile() const { return tile_data_; }

    NearestResult nearest_point(const float position[3], const float extents[3],
        uint16_t include_flags = 0xffff) const {
        NearestResult result;
        if (!ready() || position == nullptr || extents == nullptr || !finite_vec(position) ||
            !finite_vec(extents) || extents[0] <= 0.0f || extents[1] <= 0.0f || extents[2] <= 0.0f) {
            return result;
        }
        dtQueryFilter filter;
        filter.setIncludeFlags(include_flags);
        if (dtStatusFailed(query_->findNearestPoly(position, extents, &filter, &result.polygon,
            result.point.data())) || result.polygon == 0) {
            result.status = QueryStatus::NoPath;
            return result;
        }
        const dtMeshTile* tile = nullptr;
        const dtPoly* poly = nullptr;
        if (dtStatusFailed(mesh_->getTileAndPolyByRef(result.polygon, &tile, &poly)) ||
            tile == nullptr || poly == nullptr) {
            result.status = QueryStatus::NoPath;
            return result;
        }
        result.area = poly->getArea();
        result.status = QueryStatus::Success;
        return result;
    }

    RaycastResult raycast(const float start[3], const float end[3], const float extents[3],
        uint16_t include_flags = 0xffff) const {
        RaycastResult result;
        const NearestResult nearest = nearest_point(start, extents, include_flags);
        if (nearest.status != QueryStatus::Success || end == nullptr || !finite_vec(end)) {
            return result;
        }
        dtQueryFilter filter;
        filter.setIncludeFlags(include_flags);
        float t = 0.0f;
        float normal[3]{};
        const dtStatus status = query_->raycast(nearest.polygon, nearest.point.data(), end,
            &filter, &t, normal, nullptr, nullptr, 0);
        if (dtStatusFailed(status)) {
            result.status = QueryStatus::NoPath;
            return result;
        }
        result.status = QueryStatus::Success;
        result.hit_wall = t < 1.0f;
        result.fraction = t;
        result.normal = {normal[0], normal[1], normal[2]};
        return result;
    }

    QueryStatus query_path(const float start[3], const float end[3], const float extents[3],
        uint16_t include_flags = 0xffff) const {
        PathResult ignored;
        return query_path(start, end, extents, ignored, include_flags);
    }

    QueryStatus query_path(const float start[3], const float end[3], const float extents[3],
        PathResult& result, uint16_t include_flags = 0xffff) const {
        result = {};
        result.status = QueryStatus::InvalidInput;
        if (!ready() || start == nullptr || end == nullptr || extents == nullptr ||
            !finite_vec(start) || !finite_vec(end) || !finite_vec(extents) ||
            extents[0] <= 0.0f || extents[1] <= 0.0f || extents[2] <= 0.0f) {
            return result.status;
        }
        dtQueryFilter filter;
        filter.setIncludeFlags(include_flags);
        dtPolyRef start_ref = 0;
        dtPolyRef end_ref = 0;
        float start_point[3]{};
        float end_point[3]{};
        if (dtStatusFailed(query_->findNearestPoly(start, extents, &filter, &start_ref, start_point)) ||
            dtStatusFailed(query_->findNearestPoly(end, extents, &filter, &end_ref, end_point)) ||
            start_ref == 0 || end_ref == 0) {
            result.status = QueryStatus::NoPath;
            return result.status;
        }
        int count = 0;
        const dtStatus status = query_->findPath(start_ref, end_ref, start_point, end_point,
            &filter, result.polygons.data(), &count, MAX_PATH_POLYS);
        if (dtStatusFailed(status) || count <= 0) {
            result.status = QueryStatus::NoPath;
            return result.status;
        }
        result.polygon_count = std::min(count, MAX_PATH_POLYS);
        const bool truncated = count > MAX_PATH_POLYS;
        int straight_count = 0;
        const dtStatus straight_status = query_->findStraightPath(start_point, end_point,
            result.polygons.data(), result.polygon_count, result.points.data(), nullptr,
            nullptr, &straight_count, MAX_STRAIGHT_POINTS);
        if (dtStatusFailed(straight_status) || straight_count <= 0) {
            result.status = truncated ? QueryStatus::Capacity : QueryStatus::NoPath;
            return result.status;
        }
        result.point_count = std::min(straight_count, MAX_STRAIGHT_POINTS);
        result.status = truncated || straight_count > MAX_STRAIGHT_POINTS
            ? QueryStatus::Partial : QueryStatus::Success;
        return result.status;
    }

private:
    friend bool bake(const BakeInput&, NavMeshArtifact&, std::string&);

    static bool finite_vec(const float value[3]) {
        return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
    }

    dtNavMesh* mesh_ = nullptr;
    dtNavMeshQuery* query_ = nullptr;
    BakeMetadata metadata_{};
    std::vector<unsigned char> tile_data_;
    bool ready_ = false;
};

inline bool valid_input(const BakeInput& input, std::string& error) {
    if (input.vertices == nullptr || input.indices == nullptr || input.vertex_count < 3 ||
        input.triangle_count < 1 || input.vertex_count > MAX_VERTICES ||
        input.triangle_count > MAX_TRIANGLES || !std::isfinite(input.cell_size) ||
        !std::isfinite(input.cell_height) || input.cell_size < 0.001f ||
        input.cell_height < 0.001f || input.cell_size > 1000.0f ||
        input.cell_height > 1000.0f || !std::isfinite(input.agent.height) ||
        !std::isfinite(input.agent.radius) || !std::isfinite(input.agent.climb) ||
        !std::isfinite(input.agent.slope) || input.agent.height <= 0.0f ||
        input.agent.radius < 0.0f || input.agent.climb < 0.0f ||
        input.agent.slope <= 0.0f || input.agent.slope > 90.0f) {
        error = "invalid navmesh bake input";
        return false;
    }
    if (static_cast<double>(input.agent.height) / input.cell_height > MAX_CONFIG_SCALE ||
        static_cast<double>(input.agent.radius) / input.cell_size > MAX_CONFIG_SCALE ||
        static_cast<double>(input.agent.climb) / input.cell_height > MAX_CONFIG_SCALE ||
        6.0 / input.cell_size > MAX_CONFIG_SCALE) {
        error = "navmesh agent or cell scale exceeds bake bounds";
        return false;
    }
    for (int i = 0; i < input.vertex_count * 3; ++i) {
        if (!std::isfinite(input.vertices[i])) {
            error = "non-finite navmesh vertex";
            return false;
        }
    }
    for (int i = 0; i < input.triangle_count * 3; ++i) {
        if (input.indices[i] < 0 || input.indices[i] >= input.vertex_count) {
            error = "navmesh triangle index out of range";
            return false;
        }
    }
    if (input.areas != nullptr) {
        for (int i = 0; i < input.triangle_count; ++i) {
            if (input.areas[i] >= DT_MAX_AREAS) {
                error = "navmesh area id exceeds Detour's area limit";
                return false;
            }
        }
    }
    return true;
}

inline bool bake(const BakeInput& input, NavMeshArtifact& artifact, std::string& error) {
    artifact.reset();
    if (!valid_input(input, error)) {
        return false;
    }
    rcContext context(false);
    rcConfig config{};
    config.cs = input.cell_size;
    config.ch = input.cell_height;
    config.walkableSlopeAngle = input.agent.slope;
    config.walkableHeight = static_cast<int>(std::ceil(input.agent.height / config.ch));
    config.walkableClimb = static_cast<int>(std::floor(input.agent.climb / config.ch));
    config.walkableRadius = static_cast<int>(std::ceil(input.agent.radius / config.cs));
    config.maxEdgeLen = static_cast<int>(6.0f / config.cs);
    config.maxSimplificationError = config.cs * 2.0f;
    config.minRegionArea = 4 * 4;
    config.mergeRegionArea = 8 * 8;
    config.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
    config.detailSampleDist = config.cs * 6.0f;
    config.detailSampleMaxError = config.ch;
    rcCalcBounds(input.vertices, input.vertex_count, config.bmin, config.bmax);
    config.bmin[0] -= 1.0f; config.bmin[1] -= 1.0f; config.bmin[2] -= 1.0f;
    config.bmax[0] += 1.0f; config.bmax[1] += input.agent.height; config.bmax[2] += 1.0f;
    const double grid_width = (static_cast<double>(config.bmax[0]) - config.bmin[0]) / config.cs;
    const double grid_height = (static_cast<double>(config.bmax[2]) - config.bmin[2]) / config.cs;
    const double estimated_width = std::floor(grid_width + 0.5);
    const double estimated_height = std::floor(grid_height + 0.5);
    if (!std::isfinite(estimated_width) || !std::isfinite(estimated_height) ||
        estimated_width <= 0.0 || estimated_height <= 0.0 ||
        estimated_width > MAX_GRID_DIMENSION || estimated_height > MAX_GRID_DIMENSION ||
        estimated_width * estimated_height > MAX_GRID_CELLS) {
        error = "navmesh grid exceeds configured bake bounds";
        return false;
    }
    rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);
    if (config.width <= 0 || config.height <= 0 ||
        config.width > MAX_GRID_DIMENSION || config.height > MAX_GRID_DIMENSION ||
        static_cast<uint64_t>(config.width) * static_cast<uint64_t>(config.height) > MAX_GRID_CELLS) {
        error = "navmesh grid exceeds configured bake bounds";
        return false;
    }
    rcHeightfield* heightfield = rcAllocHeightfield();
    rcCompactHeightfield* compact = rcAllocCompactHeightfield();
    rcContourSet* contours = rcAllocContourSet();
    rcPolyMesh* poly_mesh = rcAllocPolyMesh();
    rcPolyMeshDetail* detail_mesh = rcAllocPolyMeshDetail();
    if (heightfield == nullptr || compact == nullptr || contours == nullptr ||
        poly_mesh == nullptr || detail_mesh == nullptr) {
        error = "navmesh allocation failed";
        if (heightfield) rcFreeHeightField(heightfield);
        if (compact) rcFreeCompactHeightfield(compact);
        if (contours) rcFreeContourSet(contours);
        if (poly_mesh) rcFreePolyMesh(poly_mesh);
        if (detail_mesh) rcFreePolyMeshDetail(detail_mesh);
        return false;
    }
    std::vector<unsigned char> generated_areas(input.triangle_count, 0);
    if (input.areas != nullptr) {
        generated_areas.assign(input.areas, input.areas + input.triangle_count);
    } else {
        rcMarkWalkableTriangles(&context, input.agent.slope, input.vertices, input.vertex_count,
            input.indices, input.triangle_count, generated_areas.data());
    }
    bool ok = rcCreateHeightfield(&context, *heightfield, config.width, config.height,
        config.bmin, config.bmax, config.cs, config.ch) &&
        rcRasterizeTriangles(&context, input.vertices, input.vertex_count, input.indices,
            generated_areas.data(), input.triangle_count, *heightfield, config.walkableClimb);
    if (ok) {
        rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);
        ok = rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb,
            *heightfield, *compact) && rcErodeWalkableArea(&context, config.walkableRadius, *compact) &&
            rcBuildDistanceField(&context, *compact) && rcBuildRegions(&context, *compact, 0,
                config.minRegionArea, config.mergeRegionArea) &&
            rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours) &&
            rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *poly_mesh) &&
            rcBuildPolyMeshDetail(&context, *poly_mesh, *compact, config.detailSampleDist,
                config.detailSampleMaxError, *detail_mesh);
    }
    if (!ok || poly_mesh->npolys <= 0 || poly_mesh->npolys > MAX_POLYS) {
        error = "navmesh raster or polygon build failed";
        rcFreeHeightField(heightfield); rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours); rcFreePolyMesh(poly_mesh); rcFreePolyMeshDetail(detail_mesh);
        return false;
    }
    for (int i = 0; i < poly_mesh->npolys; ++i) {
        if (poly_mesh->areas[i] == RC_WALKABLE_AREA) poly_mesh->areas[i] = 1;
        poly_mesh->flags[i] = poly_mesh->areas[i] == RC_NULL_AREA ? 0 : 1;
    }
    dtNavMeshCreateParams params{};
    params.verts = poly_mesh->verts; params.vertCount = poly_mesh->nverts;
    params.polys = poly_mesh->polys; params.polyAreas = poly_mesh->areas;
    params.polyFlags = poly_mesh->flags; params.polyCount = poly_mesh->npolys;
    params.nvp = poly_mesh->nvp; params.detailMeshes = detail_mesh->meshes;
    params.detailVerts = detail_mesh->verts; params.detailVertsCount = detail_mesh->nverts;
    params.detailTris = detail_mesh->tris; params.detailTriCount = detail_mesh->ntris;
    params.walkableHeight = input.agent.height; params.walkableRadius = input.agent.radius;
    params.walkableClimb = input.agent.climb; rcVcopy(params.bmin, poly_mesh->bmin);
    rcVcopy(params.bmax, poly_mesh->bmax); params.cs = config.cs; params.ch = config.ch;
    params.buildBvTree = true;
    unsigned char* nav_data = nullptr;
    int nav_data_size = 0;
    ok = dtCreateNavMeshData(&params, &nav_data, &nav_data_size);
    if (!ok || nav_data == nullptr || nav_data_size <= 0) {
        error = "detour tile serialization failed";
        rcFreeHeightField(heightfield); rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours); rcFreePolyMesh(poly_mesh); rcFreePolyMeshDetail(detail_mesh);
        return false;
    }
    dtNavMesh* mesh = dtAllocNavMesh();
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    if (mesh == nullptr || query == nullptr || dtStatusFailed(mesh->init(nav_data, nav_data_size, DT_TILE_FREE_DATA)) ||
        dtStatusFailed(query->init(mesh, MAX_QUERY_NODES))) {
        error = "detour runtime allocation failed";
        if (query) dtFreeNavMeshQuery(query);
        if (mesh) dtFreeNavMesh(mesh); else dtFree(nav_data);
        rcFreeHeightField(heightfield); rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours); rcFreePolyMesh(poly_mesh); rcFreePolyMeshDetail(detail_mesh);
        return false;
    }
    artifact.mesh_ = mesh;
    artifact.query_ = query;
    artifact.tile_data_.assign(nav_data, nav_data + nav_data_size);
    artifact.metadata_ = BakeMetadata{input.source_generation, 1, poly_mesh->npolys, nav_data_size, config.width, config.height};
    artifact.ready_ = true;
    rcFreeHeightField(heightfield); rcFreeCompactHeightfield(compact);
    rcFreeContourSet(contours); rcFreePolyMesh(poly_mesh); rcFreePolyMeshDetail(detail_mesh);
    return true;
}

struct TileHandle {
    static constexpr uint32_t INVALID_SLOT = UINT32_MAX;
    uint32_t slot = INVALID_SLOT;
    uint32_t generation = 0;
};

class NavMeshTileStore {
public:
    static constexpr uint32_t MAX_TILES = 8;

    TileHandle publish(const BakeInput& input, std::string& error) {
        const uint32_t slot = free_slot();
        if (slot == MAX_TILES) return {};
        auto artifact = std::make_unique<NavMeshArtifact>();
        if (!bake(input, *artifact, error)) return {};
        Tile& tile = tiles_[slot];
        if (tile.generation == UINT32_MAX) return {};
        tile.generation = tile.generation == 0 ? 1 : tile.generation + 1;
        tile.artifact = std::move(artifact);
        return TileHandle{slot, tile.generation};
    }

    bool live(TileHandle handle) const {
        return handle.slot < MAX_TILES && tiles_[handle.slot].artifact != nullptr &&
            tiles_[handle.slot].generation == handle.generation;
    }

    bool unload(TileHandle handle) {
        if (!live(handle)) return false;
        tiles_[handle.slot].artifact.reset();
        return true;
    }

    QueryStatus query_path(TileHandle handle, const float start[3], const float end[3],
        const float extents[3], PathResult& result, uint16_t include_flags = 0xffff) const {
        if (!live(handle)) {
            result = {};
            result.status = QueryStatus::InvalidInput;
            return result.status;
        }
        return tiles_[handle.slot].artifact->query_path(start, end, extents, result, include_flags);
    }

private:
    struct Tile {
        std::unique_ptr<NavMeshArtifact> artifact;
        uint32_t generation = 0;
    };

    uint32_t free_slot() const {
        for (uint32_t index = 0; index < MAX_TILES; ++index) {
            if (tiles_[index].artifact == nullptr) return index;
        }
        return MAX_TILES;
    }

    std::array<Tile, MAX_TILES> tiles_{};
};

} // namespace probe::nav
