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

constexpr float RAMP_HALF_SINE = 0.17364818f; // 20 degrees about -X: rises toward +Z
constexpr float RAMP_HALF_COSINE = 0.98480775f;

// A floor with a platform (top 2 m) reached by a tilted ramp, a one-way drop
// link off its east edge, a pen whose west wall has a marked doorway (area
// 3), and a mud strip (area 4) across the west of the floor.
bool stage_links() {
    const auto ok = [](int32_t status) { return status == ELISA_NAVIGATION_OK; };
    return ok(elisa_navigation_v1_begin()) &&
        ok(elisa_navigation_v1_add_box(0.0f, -0.5f, 0.0f, 10.0f, 0.5f, 10.0f)) &&
        ok(elisa_navigation_v1_add_box(0.0f, 1.0f, 6.0f, 2.0f, 1.0f, 2.0f)) &&
        ok(elisa_navigation_v1_add_oriented_box(0.0f, 0.9f, 1.3f, 1.5f, 0.15f, 3.0f,
            -RAMP_HALF_SINE, 0.0f, 0.0f, RAMP_HALF_COSINE)) &&
        ok(elisa_navigation_v1_add_box(6.0f, 0.6f, -4.0f, 2.0f, 0.6f, 0.25f)) &&
        ok(elisa_navigation_v1_add_box(6.0f, 0.6f, -8.0f, 2.0f, 0.6f, 0.25f)) &&
        ok(elisa_navigation_v1_add_box(8.0f, 0.6f, -6.0f, 0.25f, 0.6f, 2.0f)) &&
        ok(elisa_navigation_v1_add_box(4.0f, 0.6f, -7.3f, 0.25f, 0.6f, 0.7f)) &&
        ok(elisa_navigation_v1_add_box(4.0f, 0.6f, -4.7f, 0.25f, 0.6f, 0.7f)) &&
        ok(elisa_navigation_v1_mark_area(3.5f, -0.5f, -6.6f, 4.5f, 0.5f, -5.4f, 3)) &&
        ok(elisa_navigation_v1_mark_area(-6.0f, -0.5f, -10.5f, -4.0f, 0.5f, -1.0f, 4)) &&
        ok(elisa_navigation_v1_add_link(1.6f, 2.0f, 6.0f, 3.0f, 0.0f, 6.0f, 0.4f, 0));
}

struct Walk {
    int32_t route = -1;
    uint32_t count = 0;
    float length = 0.0f;
    float max_x = -1.0e9f, max_y = -1.0e9f, max_z = -1.0e9f;
    bool link = false;
    bool through_door = false;
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

Walk walk(uint32_t slot, uint32_t generation, float sx, float sy, float sz,
    float ex, float ey, float ez) {
    Walk result;
    if (elisa_navigation_v1_query_path(slot, generation, sx, sy, sz, ex, ey, ez, 1.0f, 1.0f,
            1.0f, &result.route, &result.count) != ELISA_NAVIGATION_OK) {
        result.route = -1;
        return result;
    }
    for (uint32_t index = 0; index < result.count; ++index) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        int32_t link = 0;
        if (elisa_navigation_v1_route_point(index, &x, &y, &z) != ELISA_NAVIGATION_OK ||
            elisa_navigation_v1_route_point_link(index, &link) != ELISA_NAVIGATION_OK) {
            result.route = -1;
            return result;
        }
        if (index > 0) result.length += std::hypot(x - result.x, z - result.z);
        result.max_x = std::fmax(result.max_x, x);
        result.max_y = std::fmax(result.max_y, y);
        result.max_z = std::fmax(result.max_z, z);
        result.link = result.link || link == 1;
        result.through_door = result.through_door ||
            (std::fabs(x - 4.0f) < 0.6f && z > -6.6f && z < -5.4f);
        result.x = x; result.y = y; result.z = z;
    }
    return result;
}

int links_doors_and_costs() {
    if (!expect(elisa_navigation_v1_add_oriented_box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f,
            0.5f, 0.0f, 0.0f, 0.5f) == ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_mark_area(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 2) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_mark_area(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 16) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_mark_area(1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 3) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_add_link(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_add_link(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 2) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT, "invalid ramps, areas and links are rejected")) return 30;
    uint32_t slot = 0, generation = 0;
    if (!expect(stage_links() && bake(slot, generation) == ELISA_NAVIGATION_OK,
            "linked course bakes")) return 31;
    const Walk up = walk(slot, generation, 0.0f, 0.0f, -3.0f, 0.0f, 2.0f, 6.0f);
    if (!expect(up.route == ELISA_NAVIGATION_ROUTE_FOUND && up.y > 1.8f && !up.link,
            "the ramp leads onto the platform")) return 32;
    const Walk drop = walk(slot, generation, 0.0f, 2.0f, 6.0f, 5.0f, 0.0f, 6.0f);
    if (!expect(drop.route == ELISA_NAVIGATION_ROUTE_FOUND && drop.link && drop.length < 8.0f &&
            drop.y < 0.5f, "the drop link leads off the platform")) return 33;
    const Walk climb = walk(slot, generation, 5.0f, 0.0f, 6.0f, 0.0f, 2.0f, 6.0f);
    if (!expect(climb.route == ELISA_NAVIGATION_ROUTE_FOUND && !climb.link &&
            climb.length > 8.0f, "the one-way link is not climbed")) return 34;
    const Walk enter = walk(slot, generation, -3.0f, 0.0f, -3.0f, 6.0f, 0.0f, -6.0f);
    if (!expect(enter.route == ELISA_NAVIGATION_ROUTE_FOUND && enter.through_door,
            "the pen is entered through its doorway")) return 35;
    int32_t hit = -1;
    float fraction = -1.0f;
    const auto ray = [&]() {
        return elisa_navigation_v1_raycast(slot, generation, 2.0f, 0.0f, -6.0f, 6.0f, 0.0f,
            -6.0f, 1.0f, 1.0f, 1.0f, &hit, &fraction);
    };
    if (!expect(ray() == ELISA_NAVIGATION_OK && hit == 0 && fraction == 1.0f,
            "a ray through the open doorway is clear")) return 36;
    const Walk shut = (elisa_navigation_v1_set_area(slot, generation, 3, 0, 1.0f) ==
        ELISA_NAVIGATION_OK) ? walk(slot, generation, -3.0f, 0.0f, -3.0f, 6.0f, 0.0f, -6.0f) : Walk{};
    if (!expect(shut.route == ELISA_NAVIGATION_ROUTE_UNREACHABLE && shut.x < 4.0f,
            "a closed doorway strands the route outside the pen")) return 37;
    if (!expect(ray() == ELISA_NAVIGATION_OK && hit == 1 && fraction > 0.0f && fraction < 1.0f,
            "a ray stops at the closed doorway")) return 38;
    const Walk direct = walk(slot, generation, -8.0f, 0.0f, -5.0f, -2.0f, 0.0f, -5.0f);
    const Walk around = (elisa_navigation_v1_set_area(slot, generation, 4, 1, 50.0f) ==
        ELISA_NAVIGATION_OK) ? walk(slot, generation, -8.0f, 0.0f, -5.0f, -2.0f, 0.0f, -5.0f) : Walk{};
    if (!expect(direct.route == ELISA_NAVIGATION_ROUTE_FOUND && direct.count == 2 &&
            around.route == ELISA_NAVIGATION_ROUTE_FOUND && around.max_z > -1.3f,
            "an expensive area is walked around")) return 39;
    if (!expect(elisa_navigation_v1_set_area(slot, generation, 0, 1, 1.0f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_set_area(slot, generation, 16, 1, 1.0f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_set_area(slot, generation, 3, 1, 0.5f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_set_area(slot, generation, 3, 2, 1.0f) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT &&
            elisa_navigation_v1_set_area(slot, generation + 1, 3, 1, 1.0f) ==
            ELISA_NAVIGATION_STALE_HANDLE, "invalid area settings are rejected")) return 40;
    uint32_t fresh_slot = 0, fresh_generation = 0;
    const bool rebaked = bake(fresh_slot, fresh_generation) == ELISA_NAVIGATION_OK;
    const Walk fresh = walk(fresh_slot, fresh_generation, -3.0f, 0.0f, -3.0f, 6.0f, 0.0f, -6.0f);
    const Walk old = walk(slot, generation, -3.0f, 0.0f, -3.0f, 6.0f, 0.0f, -6.0f);
    if (!expect(rebaked && fresh.route == ELISA_NAVIGATION_ROUTE_FOUND &&
            old.route == ELISA_NAVIGATION_ROUTE_UNREACHABLE,
            "area settings belong to one mesh and a new bake starts open")) return 41;
    int32_t link = 0;
    if (!expect(elisa_navigation_v1_route_point_link(old.count, &link) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT && elisa_navigation_v1_raycast(slot, generation,
            NAN, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, &hit, &fraction) ==
            ELISA_NAVIGATION_INVALID_ARGUMENT, "invalid link and ray queries")) return 42;
    if (!expect(elisa_navigation_v1_unload(slot, generation) == ELISA_NAVIGATION_OK &&
            elisa_navigation_v1_unload(fresh_slot, fresh_generation) == ELISA_NAVIGATION_OK &&
            elisa_navigation_v1_raycast(slot, generation, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f,
            1.0f, 1.0f, 1.0f, &hit, &fraction) == ELISA_NAVIGATION_STALE_HANDLE,
            "linked meshes unload")) return 43;
    bool volumes = elisa_navigation_v1_begin() == ELISA_NAVIGATION_OK;
    bool links = volumes;
    for (uint32_t index = 0; index < ELISA_NAVIGATION_MAX_AREA_VOLUMES; ++index) {
        volumes = volumes && elisa_navigation_v1_mark_area(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 3) ==
            ELISA_NAVIGATION_OK;
        links = links && elisa_navigation_v1_add_link(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1) ==
            ELISA_NAVIGATION_OK;
    }
    if (!expect(volumes && links &&
            elisa_navigation_v1_mark_area(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, 3) ==
            ELISA_NAVIGATION_CAPACITY &&
            elisa_navigation_v1_add_link(0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.5f, 1) ==
            ELISA_NAVIGATION_CAPACITY, "area volume and link limits are enforced")) return 44;
    return 0;
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
    if (const int failure = links_doors_and_costs(); failure != 0) return failure;
    std::cout << "Navigation native service tests passed.\n";
    return 0;
}
