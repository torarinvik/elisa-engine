#include "../native/navigation_service_abi.h"

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "navigation service test failed: " << message << '\n';
    return condition;
}

// A 20 m floor with a 1.2 m wall across the middle (open past |z| = 4) and a
// closed pen in one corner. The agent matches the character course capsule.
bool stage_course() {
    return elisa_navigation_v1_begin() == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(0.0f, -0.5f, 0.0f, 10.0f, 0.5f, 10.0f) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(0.0f, 0.6f, 0.0f, 0.25f, 0.6f, 4.0f) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(6.0f, 0.6f, 5.0f, 2.0f, 0.6f, 0.25f) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(6.0f, 0.6f, 9.0f, 2.0f, 0.6f, 0.25f) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(4.0f, 0.6f, 7.0f, 0.25f, 0.6f, 2.0f) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_box(8.0f, 0.6f, 7.0f, 0.25f, 0.6f, 2.0f) == ELISA_NAVIGATION_OK;
}

int32_t bake(uint32_t& slot, uint32_t& generation) {
    return elisa_navigation_v1_bake(0.25f, 1.4f, 0.35f, 45.0f, 0.1f, 0.05f, &slot, &generation);
}

int32_t query(uint32_t slot, uint32_t generation, float sx, float sz, float ex, float ey, float ez,
    int32_t& route, uint32_t& count) {
    return elisa_navigation_v1_query_path(slot, generation, sx, 0.0f, sz, ex, ey, ez,
        1.0f, 1.0f, 1.0f, &route, &count);
}

} // namespace

int main() {
    uint32_t slot = 0;
    uint32_t generation = 0;
    if (!expect(elisa_navigation_abi_version() == 1, "abi version")) return 1;
    if (!expect(elisa_navigation_v1_begin() == ELISA_NAVIGATION_OK &&
            bake(slot, generation) == ELISA_NAVIGATION_INVALID_ARGUMENT,
            "bake without geometry is rejected")) return 2;
    if (!expect(elisa_navigation_v1_add_box(0.0f, 0.0f, 0.0f, -1.0f, 1.0f, 1.0f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_add_box(NAN, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT, "invalid boxes are rejected")) return 3;
    if (!expect(stage_course() && bake(slot, generation) == ELISA_NAVIGATION_OK &&
            generation != 0, "course bakes")) return 4;

    int32_t route = -1;
    uint32_t count = 0;
    if (!expect(query(slot, generation, -5.0f, 0.0f, 5.0f, 0.0f, 0.0f, route, count) ==
            ELISA_NAVIGATION_OK && route == ELISA_NAVIGATION_ROUTE_FOUND && count >= 3,
            "route around the wall")) return 5;
    bool rounds_wall = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
    for (uint32_t index = 0; index < count; ++index) {
        if (!expect(elisa_navigation_v1_route_point(index, &x, &y, &z) == ELISA_NAVIGATION_OK,
                "read route point")) return 6;
        rounds_wall = rounds_wall || std::fabs(z) >= 4.0f;
    }
    if (!expect(rounds_wall && std::fabs(x - 5.0f) < 0.2f && std::fabs(z) < 0.2f,
            "route leaves the wall's span and ends at the goal")) return 7;
    if (!expect(elisa_navigation_v1_route_point(count, &x, &y, &z) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT, "route point past the end")) return 8;

    if (!expect(query(slot, generation, -5.0f, 0.0f, 6.0f, 0.0f, 7.0f, route, count) ==
            ELISA_NAVIGATION_OK && route == ELISA_NAVIGATION_ROUTE_UNREACHABLE && count >= 1,
            "goal inside the pen is unreachable")) return 9;
    if (!expect(elisa_navigation_v1_route_point(count - 1, &x, &y, &z) == ELISA_NAVIGATION_OK &&
            !(x > 4.0f && x < 8.0f && z > 5.0f && z < 9.0f),
            "unreachable route stops outside the pen")) return 10;
    if (!expect(query(slot, generation, -5.0f, 0.0f, 5.0f, 40.0f, 0.0f, route, count) ==
            ELISA_NAVIGATION_OK && route == ELISA_NAVIGATION_ROUTE_OFF_MESH && count == 0,
            "goal far above the floor is off the mesh")) return 11;
    int32_t found = -1;
    float nx = 0.0f, ny = 0.0f, nz = 0.0f;
    if (!expect(elisa_navigation_v1_nearest(slot, generation, -5.0f, 0.5f, 0.0f, 1.0f, 1.0f,
            1.0f, &found, &nx, &ny, &nz) == ELISA_NAVIGATION_OK &&
            found == ELISA_NAVIGATION_ROUTE_FOUND && std::fabs(ny) < 0.2f,
            "nearest point snaps to the floor")) return 12;

    int32_t live = 0;
    if (!expect(elisa_navigation_v1_unload(slot, generation) == ELISA_NAVIGATION_OK &&
            elisa_navigation_v1_live(slot, generation, &live) == ELISA_NAVIGATION_OK && live == 0 &&
            elisa_navigation_v1_live_count() == 0, "unload releases the mesh")) return 13;
    if (!expect(query(slot, generation, -5.0f, 0.0f, 5.0f, 0.0f, 0.0f, route, count) ==
            ELISA_NAVIGATION_STALE_HANDLE && count == 0 &&
            elisa_navigation_v1_unload(slot, generation) == ELISA_NAVIGATION_STALE_HANDLE,
            "stale handle is rejected")) return 14;

    uint32_t reloaded_slot = 0;
    uint32_t reloaded_generation = 0;
    if (!expect(bake(reloaded_slot, reloaded_generation) == ELISA_NAVIGATION_OK &&
            reloaded_slot == slot && reloaded_generation != generation,
            "rebake reuses the slot with a new generation")) return 15;
    if (!expect(query(slot, generation, -5.0f, 0.0f, 5.0f, 0.0f, 0.0f, route, count) ==
            ELISA_NAVIGATION_STALE_HANDLE, "old generation stays stale after reload")) return 16;
    if (!expect(query(reloaded_slot, reloaded_generation, -5.0f, 0.0f, 5.0f, 0.0f, 0.0f, route,
            count) == ELISA_NAVIGATION_OK && route == ELISA_NAVIGATION_ROUTE_FOUND,
            "reloaded mesh routes")) return 17;

    uint32_t slots[8] = {};
    uint32_t generations[8] = {};
    slots[0] = reloaded_slot;
    generations[0] = reloaded_generation;
    for (int index = 1; index < 8; ++index) {
        if (!expect(bake(slots[index], generations[index]) == ELISA_NAVIGATION_OK,
                "fill the store")) return 18;
    }
    uint32_t extra_slot = 0;
    uint32_t extra_generation = 0;
    if (!expect(bake(extra_slot, extra_generation) == ELISA_NAVIGATION_CAPACITY &&
            elisa_navigation_v1_live_count() == 8, "ninth mesh reports capacity")) return 19;
    for (int index = 0; index < 8; ++index) {
        if (!expect(elisa_navigation_v1_unload(slots[index], generations[index]) ==
                ELISA_NAVIGATION_OK, "unload every mesh")) return 20;
    }
    if (!expect(elisa_navigation_v1_live_count() == 0, "store is empty")) return 21;
    for (int box = 6; box < static_cast<int>(ELISA_NAVIGATION_MAX_BOXES); ++box) {
        if (!expect(elisa_navigation_v1_add_box(-9.0f, 0.1f, -9.0f, 0.1f, 0.1f, 0.1f) ==
                ELISA_NAVIGATION_OK, "stage up to the box limit")) return 22;
    }
    if (!expect(elisa_navigation_v1_add_box(-9.0f, 0.1f, -9.0f, 0.1f, 0.1f, 0.1f) ==
            ELISA_NAVIGATION_CAPACITY, "box limit is enforced")) return 23;
    std::cout << "Navigation native service tests passed.\n";
    return 0;
}
