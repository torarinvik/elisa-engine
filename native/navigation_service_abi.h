#pragma once

#include <cstdint>

// Scalar-only C ABI over `probe::nav::NavMeshTileStore` for Elisa
// applications. Geometry is staged as axis-aligned boxes in Elisa world
// coordinates (metres, +Y up), baked into a generation-tagged navmesh, and
// queried for straight corridors that the caller copies out point by point.

enum ElisaNavigationStatus : int32_t {
    ELISA_NAVIGATION_OK = 0,
    ELISA_NAVIGATION_INVALID_ARGUMENT = 1,
    ELISA_NAVIGATION_CAPACITY = 2,
    ELISA_NAVIGATION_BAKE_FAILED = 3,
    ELISA_NAVIGATION_STALE_HANDLE = 4,
};

// Route outcomes written by `query_path` and `nearest`.
enum ElisaNavigationRoute : int32_t {
    // The corridor ends at the requested goal.
    ELISA_NAVIGATION_ROUTE_FOUND = 0,
    // The goal is on the mesh but not connected to the start; the corridor
    // ends at the closest reachable point.
    ELISA_NAVIGATION_ROUTE_UNREACHABLE = 1,
    // An endpoint is not near any walkable polygon.
    ELISA_NAVIGATION_ROUTE_OFF_MESH = 2,
    // The corridor exceeded the bounded polygon or point capacity.
    ELISA_NAVIGATION_ROUTE_TRUNCATED = 3,
};

enum : uint32_t {
    ELISA_NAVIGATION_MAX_BOXES = 64,
    ELISA_NAVIGATION_MAX_ROUTE_POINTS = 256,
};

extern "C" {

uint32_t elisa_navigation_abi_version();
// Discard staged geometry. Published meshes are unaffected.
int32_t elisa_navigation_v1_begin();
// Stage one solid box. Its top face is walkable when flat enough; its sides
// and bottom block agents.
int32_t elisa_navigation_v1_add_box(float center_x, float center_y, float center_z,
    float half_x, float half_y, float half_z);
// Bake the staged boxes for one agent profile and publish the result.
int32_t elisa_navigation_v1_bake(float agent_radius, float agent_height,
    float max_climb, float max_slope_degrees, float cell_size, float cell_height,
    uint32_t* slot, uint32_t* generation);
int32_t elisa_navigation_v1_live(uint32_t slot, uint32_t generation, int32_t* live);
int32_t elisa_navigation_v1_unload(uint32_t slot, uint32_t generation);
// Find a corridor and keep its points for `route_point`. The search extents
// are how far off the mesh an endpoint may lie.
int32_t elisa_navigation_v1_query_path(uint32_t slot, uint32_t generation,
    float start_x, float start_y, float start_z, float end_x, float end_y, float end_z,
    float extent_x, float extent_y, float extent_z, int32_t* route, uint32_t* point_count);
// Read a point of the most recent successful `query_path` on this thread.
int32_t elisa_navigation_v1_route_point(uint32_t index, float* x, float* y, float* z);
int32_t elisa_navigation_v1_nearest(uint32_t slot, uint32_t generation,
    float x, float y, float z, float extent_x, float extent_y, float extent_z,
    int32_t* route, float* nearest_x, float* nearest_y, float* nearest_z);
// Number of published meshes, for leak checks.
uint32_t elisa_navigation_v1_live_count();

}
