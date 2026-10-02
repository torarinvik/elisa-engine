#pragma once

#include <cstdint>

// Scalar-only C ABI over `probe::nav::NavMeshTileStore` for Elisa
// applications. Geometry is staged as solid boxes in Elisa world
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
    ELISA_NAVIGATION_MAX_AREA_VOLUMES = 16,
    ELISA_NAVIGATION_MAX_LINKS = 16,
};

// Polygon areas. Every walkable polygon starts as WALK; links are LINK; marked
// volumes take an id from FIRST_CUSTOM to LAST. Each mesh can forbid an area
// or make it cost more per metre with `set_area`.
enum : uint32_t {
    ELISA_NAVIGATION_AREA_WALK = 1,
    ELISA_NAVIGATION_AREA_LINK = 2,
    ELISA_NAVIGATION_AREA_FIRST_CUSTOM = 3,
    ELISA_NAVIGATION_AREA_LAST = 15,
};

extern "C" {

uint32_t elisa_navigation_abi_version();
// Discard staged geometry, area volumes and links. Published meshes are unaffected.
int32_t elisa_navigation_v1_begin();
// Stage one solid box. Its top face is walkable when flat enough; its sides
// and bottom block agents.
int32_t elisa_navigation_v1_add_box(float center_x, float center_y, float center_z,
    float half_x, float half_y, float half_z);
// Stage one solid box turned by the unit quaternion (qx, qy, qz, qw), such as
// a ramp. It counts toward ELISA_NAVIGATION_MAX_BOXES.
int32_t elisa_navigation_v1_add_oriented_box(float center_x, float center_y, float center_z,
    float half_x, float half_y, float half_z, float qx, float qy, float qz, float qw);
// Give walkable ground inside the axis-aligned volume a custom area id, such
// as a doorway that can be closed.
int32_t elisa_navigation_v1_mark_area(float min_x, float min_y, float min_z,
    float max_x, float max_y, float max_z, uint32_t area);
// Stage an off-mesh link, such as a drop off a ledge: agents may cross from
// `start` to `end` (and back when bidirectional). Each endpoint must lie
// within `radius` of the mesh.
int32_t elisa_navigation_v1_add_link(float start_x, float start_y, float start_z,
    float end_x, float end_y, float end_z, float radius, int32_t bidirectional);
// Bake the staged boxes for one agent profile and publish the result.
int32_t elisa_navigation_v1_bake(float agent_radius, float agent_height,
    float max_climb, float max_slope_degrees, float cell_size, float cell_height,
    uint32_t* slot, uint32_t* generation);
int32_t elisa_navigation_v1_live(uint32_t slot, uint32_t generation, int32_t* live);
// R06: ground-polygon bounds of a live navmesh, in Elisa coordinates (min then max xyz).
int32_t elisa_navigation_v1_mesh_bounds(uint32_t slot, uint32_t generation,
    float* min_x, float* min_y, float* min_z, float* max_x, float* max_y, float* max_z);
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
// Per mesh: allow or forbid an area for later queries and set its cost per
// metre (at least 1). A new bake starts with every area allowed at cost 1.
int32_t elisa_navigation_v1_set_area(uint32_t slot, uint32_t generation, uint32_t area,
    int32_t enabled, float cost);
// Whether route point `index` starts an off-mesh link.
int32_t elisa_navigation_v1_route_point_link(uint32_t index, int32_t* link_start);
// Walk a straight line over the mesh from the polygon under `start`. `hit` is
// 1 when a wall or forbidden area stops it at `fraction` of the way.
int32_t elisa_navigation_v1_raycast(uint32_t slot, uint32_t generation,
    float start_x, float start_y, float start_z, float end_x, float end_y, float end_z,
    float extent_x, float extent_y, float extent_z, int32_t* hit, float* fraction);
// Number of published meshes, for leak checks.
uint32_t elisa_navigation_v1_live_count();

// N01 cooked navmeshes (native/navigation_cook_abi.inc). `cook` turns the
// staged scene into a deterministic tiled blob of `tile_cells` cells a side,
// cached under `scene_id`: `cache_outcome` is 0 miss, 1 hit (no re-cook) or
// 2 invalidated (the scene or profile changed). Failures leave diagnostics
// {code, severity 1 warning / 2 error, subject, count} per navmesh_cook.h.
int32_t elisa_navigation_v1_cook(float agent_radius, float agent_height, float max_climb,
    float max_slope_degrees, float cell_size, float cell_height, uint32_t tile_cells, uint64_t scene_id,
    uint32_t* slot, uint32_t* generation, uint32_t* cache_outcome);
uint32_t elisa_navigation_v1_cook_diagnostic_count();
int32_t elisa_navigation_v1_cook_diagnostic(uint32_t index, uint32_t* code, uint32_t* severity,
    uint32_t* subject, uint32_t* count);
int32_t elisa_navigation_v1_cook_info(uint32_t* tiles_x, uint32_t* tiles_z, uint32_t* tiles,
    uint32_t* polygons, uint32_t* bytes, uint64_t* digest);
uint32_t elisa_navigation_v1_cook_area_polygons(uint32_t area);
uint32_t elisa_navigation_v1_overlay_count();
int32_t elisa_navigation_v1_overlay_line(uint32_t index, float* ax, float* ay, float* az,
    float* bx, float* by, float* bz, uint32_t* area, int32_t* source_box, int32_t* link);
uint32_t elisa_navigation_v1_cache_size();
int32_t elisa_navigation_v1_cache_clear();
int32_t elisa_navigation_v1_cache_save(const char* path);
int32_t elisa_navigation_v1_cache_load(const char* path);
// N02 tile streaming on cooked meshes; see navigation_stream_abi.inc.
int32_t elisa_navigation_v1_tile_loaded(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz,
    int32_t* loaded);
int32_t elisa_navigation_v1_tile_unload(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz);
int32_t elisa_navigation_v1_tile_load(uint32_t slot, uint32_t generation, uint32_t tx, uint32_t tz);

}
