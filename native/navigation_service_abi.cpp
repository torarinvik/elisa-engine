#include "navigation_service_abi.h"

#include "navmesh_service.h"

#include <array>
#include <cmath>
#include <mutex>
#include <string>
#include <vector>

namespace {

constexpr uint32_t ABI_VERSION = 1;
constexpr uint32_t BOX_VERTICES = 8;
constexpr uint32_t BOX_TRIANGLES = 12;
constexpr float MAX_EXTENT = 16384.0f;

// Corners are indexed by bit: 1 = +X, 2 = +Y, 4 = +Z. Every face winds
// counter-clockwise seen from outside, so only the top face is walkable.
constexpr std::array<int, BOX_TRIANGLES * 3> BOX_INDICES = {
    2, 6, 7, 2, 7, 3, // +Y top
    0, 1, 5, 0, 5, 4, // -Y bottom
    1, 3, 7, 1, 7, 5, // +X
    0, 4, 6, 0, 6, 2, // -X
    4, 5, 7, 4, 7, 6, // +Z
    0, 2, 3, 0, 3, 1, // -Z
};

struct Service {
    std::mutex mutex;
    std::vector<float> vertices;
    std::vector<int> indices;
    uint32_t boxes = 0;
    uint32_t bakes = 0;
    probe::nav::NavMeshTileStore store;
};

Service& service() {
    static Service instance;
    return instance;
}

// Each thread reads back the corridor it asked for last.
thread_local probe::nav::PathResult last_route;

bool finite(float value) { return std::isfinite(value) && std::fabs(value) <= MAX_EXTENT; }

bool positive(float value) { return finite(value) && value > 0.0f; }

int32_t route_code(probe::nav::QueryStatus status, bool reaches_goal, bool truncated) {
    if (status == probe::nav::QueryStatus::Success) return ELISA_NAVIGATION_ROUTE_FOUND;
    if (status == probe::nav::QueryStatus::Partial) {
        return truncated ? ELISA_NAVIGATION_ROUTE_TRUNCATED :
            (reaches_goal ? ELISA_NAVIGATION_ROUTE_FOUND : ELISA_NAVIGATION_ROUTE_UNREACHABLE);
    }
    if (status == probe::nav::QueryStatus::Capacity) return ELISA_NAVIGATION_ROUTE_TRUNCATED;
    return ELISA_NAVIGATION_ROUTE_OFF_MESH;
}

} // namespace

extern "C" {

uint32_t elisa_navigation_abi_version() { return ABI_VERSION; }

int32_t elisa_navigation_v1_begin() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    nav.vertices.clear();
    nav.indices.clear();
    nav.boxes = 0;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_add_box(float center_x, float center_y, float center_z,
    float half_x, float half_y, float half_z) {
    if (!finite(center_x) || !finite(center_y) || !finite(center_z) ||
        !positive(half_x) || !positive(half_y) || !positive(half_z)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (nav.boxes >= ELISA_NAVIGATION_MAX_BOXES) return ELISA_NAVIGATION_CAPACITY;
    const int base = static_cast<int>(nav.vertices.size() / 3);
    nav.vertices.reserve(ELISA_NAVIGATION_MAX_BOXES * BOX_VERTICES * 3);
    nav.indices.reserve(ELISA_NAVIGATION_MAX_BOXES * BOX_TRIANGLES * 3);
    for (uint32_t corner = 0; corner < BOX_VERTICES; ++corner) {
        nav.vertices.push_back(center_x + ((corner & 1) ? half_x : -half_x));
        nav.vertices.push_back(center_y + ((corner & 2) ? half_y : -half_y));
        nav.vertices.push_back(center_z + ((corner & 4) ? half_z : -half_z));
    }
    for (int index : BOX_INDICES) nav.indices.push_back(base + index);
    ++nav.boxes;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_bake(float agent_radius, float agent_height,
    float max_climb, float max_slope_degrees, float cell_size, float cell_height,
    uint32_t* slot, uint32_t* generation) {
    if (slot == nullptr || generation == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *slot = probe::nav::TileHandle::INVALID_SLOT;
    *generation = 0;
    if (!positive(agent_radius) || !positive(agent_height) || !finite(max_climb) ||
        max_climb < 0.0f || !positive(max_slope_degrees) || max_slope_degrees >= 90.0f ||
        !positive(cell_size) || !positive(cell_height)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (nav.boxes == 0) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    probe::nav::BakeInput input;
    input.vertices = nav.vertices.data();
    input.vertex_count = static_cast<int>(nav.vertices.size() / 3);
    input.indices = nav.indices.data();
    input.triangle_count = static_cast<int>(nav.indices.size() / 3);
    // No explicit areas: `bake` marks walkable triangles by the slope limit,
    // which keeps box tops and rejects their sides and bottoms.
    input.areas = nullptr;
    input.agent.radius = agent_radius;
    input.agent.height = agent_height;
    input.agent.climb = max_climb;
    input.agent.slope = max_slope_degrees;
    input.cell_size = cell_size;
    input.cell_height = cell_height;
    input.source_generation = ++nav.bakes;
    std::string error;
    const probe::nav::TileHandle handle = nav.store.publish(input, error);
    if (handle.slot == probe::nav::TileHandle::INVALID_SLOT) {
        return error.empty() ? ELISA_NAVIGATION_CAPACITY : ELISA_NAVIGATION_BAKE_FAILED;
    }
    *slot = handle.slot;
    *generation = handle.generation;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_live(uint32_t slot, uint32_t generation, int32_t* live) {
    if (live == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    *live = nav.store.live(probe::nav::TileHandle{slot, generation}) ? 1 : 0;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_unload(uint32_t slot, uint32_t generation) {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return nav.store.unload(probe::nav::TileHandle{slot, generation}) ?
        ELISA_NAVIGATION_OK : ELISA_NAVIGATION_STALE_HANDLE;
}

int32_t elisa_navigation_v1_query_path(uint32_t slot, uint32_t generation,
    float start_x, float start_y, float start_z, float end_x, float end_y, float end_z,
    float extent_x, float extent_y, float extent_z, int32_t* route, uint32_t* point_count) {
    if (route == nullptr || point_count == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *route = ELISA_NAVIGATION_ROUTE_OFF_MESH;
    *point_count = 0;
    last_route = {};
    if (!finite(start_x) || !finite(start_y) || !finite(start_z) || !finite(end_x) ||
        !finite(end_y) || !finite(end_z) || !positive(extent_x) || !positive(extent_y) ||
        !positive(extent_z)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    const float start[3] = {start_x, start_y, start_z};
    const float end[3] = {end_x, end_y, end_z};
    const float extents[3] = {extent_x, extent_y, extent_z};
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const probe::nav::TileHandle handle{slot, generation};
    if (!nav.store.live(handle)) return ELISA_NAVIGATION_STALE_HANDLE;
    const probe::nav::QueryStatus status = nav.store.query_path(handle, start, end, extents, last_route);
    const bool truncated = last_route.polygon_count >= probe::nav::MAX_PATH_POLYS ||
        last_route.point_count >= static_cast<int>(ELISA_NAVIGATION_MAX_ROUTE_POINTS);
    *route = route_code(status, last_route.reaches_goal, truncated);
    if (*route != ELISA_NAVIGATION_ROUTE_OFF_MESH) {
        *point_count = static_cast<uint32_t>(last_route.point_count);
    } else {
        last_route = {};
    }
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_route_point(uint32_t index, float* x, float* y, float* z) {
    if (x == nullptr || y == nullptr || z == nullptr ||
        index >= static_cast<uint32_t>(last_route.point_count)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    *x = last_route.points[index * 3];
    *y = last_route.points[index * 3 + 1];
    *z = last_route.points[index * 3 + 2];
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_nearest(uint32_t slot, uint32_t generation,
    float x, float y, float z, float extent_x, float extent_y, float extent_z,
    int32_t* route, float* nearest_x, float* nearest_y, float* nearest_z) {
    if (route == nullptr || nearest_x == nullptr || nearest_y == nullptr || nearest_z == nullptr) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    *route = ELISA_NAVIGATION_ROUTE_OFF_MESH;
    *nearest_x = x;
    *nearest_y = y;
    *nearest_z = z;
    if (!finite(x) || !finite(y) || !finite(z) || !positive(extent_x) ||
        !positive(extent_y) || !positive(extent_z)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    const float position[3] = {x, y, z};
    const float extents[3] = {extent_x, extent_y, extent_z};
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const probe::nav::NearestResult result = nav.store.nearest_point(
        probe::nav::TileHandle{slot, generation}, position, extents);
    if (result.status == probe::nav::QueryStatus::InvalidInput) return ELISA_NAVIGATION_STALE_HANDLE;
    if (result.status != probe::nav::QueryStatus::Success) return ELISA_NAVIGATION_OK;
    *route = ELISA_NAVIGATION_ROUTE_FOUND;
    *nearest_x = result.point[0];
    *nearest_y = result.point[1];
    *nearest_z = result.point[2];
    return ELISA_NAVIGATION_OK;
}

uint32_t elisa_navigation_v1_live_count() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return nav.store.live_count();
}

}
