#include "navigation_service_abi.h"

#include "navmesh_service.h"
#include "navmesh_bounds.h"

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
    std::vector<probe::nav::AreaVolume> volumes;
    std::vector<probe::nav::OffMeshLink> links;
    uint32_t boxes = 0;
    uint32_t bakes = 0;
    probe::nav::NavMeshTileStore store;
    // Area permissions and costs of the mesh in each store slot.
    std::array<probe::nav::QueryFilter, probe::nav::NavMeshTileStore::MAX_TILES> filters{};
};

Service& service() {
    static Service instance;
    return instance;
}

// Each thread reads back the corridor it asked for last.
thread_local probe::nav::PathResult last_route;

bool finite(float value) { return std::isfinite(value) && std::fabs(value) <= MAX_EXTENT; }

bool positive(float value) { return finite(value) && value > 0.0f; }

bool finite3(float x, float y, float z) { return finite(x) && finite(y) && finite(z); }

// Append one box's corners (turned by `rotation`, a unit quaternion x, y, z,
// w) and its twelve triangles. The caller holds the lock and checked capacity.
void push_box(Service& nav, const float center[3], const float half[3], const float rotation[4]) {
    const int base = static_cast<int>(nav.vertices.size() / 3);
    nav.vertices.reserve(ELISA_NAVIGATION_MAX_BOXES * BOX_VERTICES * 3);
    nav.indices.reserve(ELISA_NAVIGATION_MAX_BOXES * BOX_TRIANGLES * 3);
    const float qx = rotation[0], qy = rotation[1], qz = rotation[2], qw = rotation[3];
    for (uint32_t corner = 0; corner < BOX_VERTICES; ++corner) {
        const float vx = (corner & 1) ? half[0] : -half[0];
        const float vy = (corner & 2) ? half[1] : -half[1];
        const float vz = (corner & 4) ? half[2] : -half[2];
        // v + 2w(q x v) + 2 q x (q x v)
        const float tx = 2.0f * (qy * vz - qz * vy);
        const float ty = 2.0f * (qz * vx - qx * vz);
        const float tz = 2.0f * (qx * vy - qy * vx);
        nav.vertices.push_back(center[0] + vx + qw * tx + (qy * tz - qz * ty));
        nav.vertices.push_back(center[1] + vy + qw * ty + (qz * tx - qx * tz));
        nav.vertices.push_back(center[2] + vz + qw * tz + (qx * ty - qy * tx));
    }
    for (int index : BOX_INDICES) nav.indices.push_back(base + index);
    ++nav.boxes;
}

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
    nav.volumes.clear();
    nav.links.clear();
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
    const float center[3] = {center_x, center_y, center_z};
    const float half[3] = {half_x, half_y, half_z};
    const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    push_box(nav, center, half, identity);
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_add_oriented_box(float center_x, float center_y, float center_z,
    float half_x, float half_y, float half_z, float qx, float qy, float qz, float qw) {
    if (!finite3(center_x, center_y, center_z) || !positive(half_x) || !positive(half_y) ||
        !positive(half_z) || !finite3(qx, qy, qz) || !finite(qw)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    const float length = std::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (std::fabs(length - 1.0f) > 1.0e-3f) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (nav.boxes >= ELISA_NAVIGATION_MAX_BOXES) return ELISA_NAVIGATION_CAPACITY;
    const float center[3] = {center_x, center_y, center_z};
    const float half[3] = {half_x, half_y, half_z};
    const float rotation[4] = {qx / length, qy / length, qz / length, qw / length};
    push_box(nav, center, half, rotation);
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_mark_area(float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z, uint32_t area) {
    if (!finite3(min_x, min_y, min_z) || !finite3(max_x, max_y, max_z) || min_x > max_x ||
        min_y > max_y || min_z > max_z || area < ELISA_NAVIGATION_AREA_FIRST_CUSTOM ||
        area > ELISA_NAVIGATION_AREA_LAST) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (nav.volumes.size() >= ELISA_NAVIGATION_MAX_AREA_VOLUMES) return ELISA_NAVIGATION_CAPACITY;
    probe::nav::AreaVolume volume;
    volume.min[0] = min_x; volume.min[1] = min_y; volume.min[2] = min_z;
    volume.max[0] = max_x; volume.max[1] = max_y; volume.max[2] = max_z;
    volume.area = static_cast<unsigned char>(area);
    nav.volumes.push_back(volume);
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_add_link(float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float radius, int32_t bidirectional) {
    if (!finite3(start_x, start_y, start_z) || !finite3(end_x, end_y, end_z) ||
        !positive(radius) || radius > probe::nav::MAX_LINK_RADIUS ||
        (bidirectional != 0 && bidirectional != 1)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (nav.links.size() >= ELISA_NAVIGATION_MAX_LINKS) return ELISA_NAVIGATION_CAPACITY;
    probe::nav::OffMeshLink link;
    link.start[0] = start_x; link.start[1] = start_y; link.start[2] = start_z;
    link.end[0] = end_x; link.end[1] = end_y; link.end[2] = end_z;
    link.radius = radius;
    link.bidirectional = bidirectional == 1;
    nav.links.push_back(link);
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
    input.volumes = nav.volumes.data();
    input.volume_count = static_cast<int>(nav.volumes.size());
    input.links = nav.links.data();
    input.link_count = static_cast<int>(nav.links.size());
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
    nav.filters[handle.slot] = probe::nav::QueryFilter();
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

int32_t elisa_navigation_v1_mesh_bounds(uint32_t slot, uint32_t generation,
    float* min_x, float* min_y, float* min_z, float* max_x, float* max_y, float* max_z) {
    if (min_x == nullptr || min_y == nullptr || min_z == nullptr ||
        max_x == nullptr || max_y == nullptr || max_z == nullptr) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const probe::nav::NavMeshArtifact* artifact =
        nav.store.artifact(probe::nav::TileHandle{slot, generation});
    if (artifact == nullptr) return ELISA_NAVIGATION_STALE_HANDLE;
    float minimum[3];
    float maximum[3];
    if (!probe::nav::navmesh_bounds(artifact->mesh(), minimum, maximum)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    *min_x = minimum[0]; *min_y = minimum[1]; *min_z = minimum[2];
    *max_x = maximum[0]; *max_y = maximum[1]; *max_z = maximum[2];
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
    const probe::nav::QueryStatus status = nav.store.query_path(handle, start, end, extents, last_route,
        nav.filters[slot]);
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
        probe::nav::TileHandle{slot, generation}, position, extents,
        slot < nav.filters.size() ? nav.filters[slot] : probe::nav::QueryFilter());
    if (result.status == probe::nav::QueryStatus::InvalidInput) return ELISA_NAVIGATION_STALE_HANDLE;
    if (result.status != probe::nav::QueryStatus::Success) return ELISA_NAVIGATION_OK;
    *route = ELISA_NAVIGATION_ROUTE_FOUND;
    *nearest_x = result.point[0];
    *nearest_y = result.point[1];
    *nearest_z = result.point[2];
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_set_area(uint32_t slot, uint32_t generation, uint32_t area,
    int32_t enabled, float cost) {
    if (area < ELISA_NAVIGATION_AREA_WALK || area > ELISA_NAVIGATION_AREA_LAST ||
        (enabled != 0 && enabled != 1) || !finite(cost) || cost < 1.0f) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    if (!nav.store.live(probe::nav::TileHandle{slot, generation})) return ELISA_NAVIGATION_STALE_HANDLE;
    probe::nav::QueryFilter& filter = nav.filters[slot];
    const uint16_t bit = static_cast<uint16_t>(1u << area);
    filter.exclude_flags = static_cast<uint16_t>(enabled == 1 ?
        (filter.exclude_flags & ~bit) : (filter.exclude_flags | bit));
    filter.costs[area] = cost;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_route_point_link(uint32_t index, int32_t* link_start) {
    if (link_start == nullptr || index >= static_cast<uint32_t>(last_route.point_count)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    *link_start = (last_route.point_flags[index] & DT_STRAIGHTPATH_OFFMESH_CONNECTION) != 0 ? 1 : 0;
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_raycast(uint32_t slot, uint32_t generation,
    float start_x, float start_y, float start_z, float end_x, float end_y, float end_z,
    float extent_x, float extent_y, float extent_z, int32_t* hit, float* fraction) {
    if (hit == nullptr || fraction == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *hit = 0;
    *fraction = 0.0f;
    if (!finite3(start_x, start_y, start_z) || !finite3(end_x, end_y, end_z) ||
        !positive(extent_x) || !positive(extent_y) || !positive(extent_z)) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    const float start[3] = {start_x, start_y, start_z};
    const float end[3] = {end_x, end_y, end_z};
    const float extents[3] = {extent_x, extent_y, extent_z};
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const probe::nav::TileHandle handle{slot, generation};
    if (!nav.store.live(handle)) return ELISA_NAVIGATION_STALE_HANDLE;
    const probe::nav::RaycastResult result = nav.store.raycast(handle, start, end, extents,
        nav.filters[slot]);
    // A start off the mesh counts as blocked at once.
    if (result.status != probe::nav::QueryStatus::Success) {
        *hit = 1;
        return ELISA_NAVIGATION_OK;
    }
    *hit = result.hit_wall ? 1 : 0;
    *fraction = result.hit_wall ? result.fraction : 1.0f;
    return ELISA_NAVIGATION_OK;
}

uint32_t elisa_navigation_v1_live_count() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return nav.store.live_count();
}

}

#include "navigation_cook_abi.inc"
