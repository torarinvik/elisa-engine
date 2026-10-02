// N01: cooked tiled navmeshes are byte-for-byte deterministic, cache hits and
// invalidations follow source and profile changes, and bad input yields
// diagnostics instead of a mesh. Run under ASan/UBSan by run_boundary_sanitized.py.
#include "../native/navigation_service_abi.h"
#include "../native/navmesh_cook_cache.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace {

namespace ck = probe::nav::cook;

constexpr float RADIUS = 0.25f, HEIGHT = 1.4f, CLIMB = 0.35f, SLOPE = 45.0f, CS = 0.1f, CH = 0.05f;
constexpr uint32_t TILE = 64;
constexpr uint32_t MEZZANINE_BOX = 10;

bool expect(bool condition, const char* message) {
    if (!condition) std::cerr << "navmesh cook test failed: " << message << '\n';
    return condition;
}

bool box(float x, float y, float z, float hx, float hy, float hz) {
    return elisa_navigation_v1_add_box(x, y, z, hx, hy, hz) == ELISA_NAVIGATION_OK;
}

// Two rooms split by a wall with a doorway, a pillar, six stairs up to a
// mezzanine, a door area and a drop link from the mezzanine back down.
bool stage_scene(float pillar_x) {
    bool ok = elisa_navigation_v1_begin() == ELISA_NAVIGATION_OK &&
        box(6.0f, -0.1f, 0.0f, 11.0f, 0.1f, 5.0f) &&            // 0 floor
        box(6.0f, 1.0f, -3.0f, 0.1f, 1.0f, 2.0f) &&             // 1 wall
        box(6.0f, 1.0f, 3.0f, 0.1f, 1.0f, 2.0f) &&              // 2 wall
        box(pillar_x, 1.0f, 2.0f, 0.5f, 1.0f, 0.5f);            // 3 pillar
    for (int step = 0; ok && step < 6; ++step) {                // 4..9 stairs
        const float top = 0.25f * static_cast<float>(step + 1);
        ok = box(8.75f + 0.5f * static_cast<float>(step), top * 0.5f, -3.0f, 0.25f, top * 0.5f, 1.0f);
    }
    return ok && box(14.0f, 0.75f, -3.0f, 2.5f, 0.75f, 2.0f) && // 10 mezzanine
        elisa_navigation_v1_mark_area(5.5f, -0.5f, -1.0f, 6.5f, 0.5f, 1.0f, 3) == ELISA_NAVIGATION_OK &&
        elisa_navigation_v1_add_link(14.0f, 1.5f, -1.4f, 14.0f, 0.0f, -0.4f, 0.4f, 0) == ELISA_NAVIGATION_OK;
}

int32_t cook(uint64_t scene, uint32_t& slot, uint32_t& generation, uint32_t& outcome,
    float radius = RADIUS, uint32_t tile = TILE) {
    return elisa_navigation_v1_cook(radius, HEIGHT, CLIMB, SLOPE, CS, CH, tile, scene, &slot, &generation, &outcome);
}

struct Info { uint32_t tiles_x = 0, tiles_z = 0, tiles = 0, polygons = 0, bytes = 0; uint64_t digest = 0; };

Info info() {
    Info result;
    elisa_navigation_v1_cook_info(&result.tiles_x, &result.tiles_z, &result.tiles, &result.polygons,
        &result.bytes, &result.digest);
    return result;
}

bool has_diagnostic(ck::Code wanted, ck::Severity severity) {
    for (uint32_t i = 0; i < elisa_navigation_v1_cook_diagnostic_count(); ++i) {
        uint32_t code = 0, level = 0, subject = 0, count = 0;
        if (elisa_navigation_v1_cook_diagnostic(i, &code, &level, &subject, &count) == ELISA_NAVIGATION_OK &&
            code == static_cast<uint32_t>(wanted) && level == static_cast<uint32_t>(severity)) return true;
    }
    return false;
}

// Two cooks of one hand-built input, straight through the native cook, match byte for byte.
int direct_determinism() {
    std::vector<float> vertices;
    std::vector<int> indices;
    const float tops[2][4] = {{-6.0f, 0.0f, -4.0f, 10.0f}, {6.0f, 0.0f, 4.0f, 10.0f}};
    for (const auto& quad : tops) {
        const int base = static_cast<int>(vertices.size() / 3);
        const float x0 = quad[0] - 5.0f, x1 = quad[0] + 5.0f, z0 = quad[2] - 5.0f, z1 = quad[2] + 5.0f;
        const float corners[12] = {x0, quad[1], z0, x1, quad[1], z0, x1, quad[1], z1, x0, quad[1], z1};
        vertices.insert(vertices.end(), corners, corners + 12);
        const int tris[6] = {base, base + 2, base + 1, base, base + 3, base + 2};
        indices.insert(indices.end(), tris, tris + 6);
    }
    probe::nav::BakeInput input;
    input.vertices = vertices.data();
    input.vertex_count = static_cast<int>(vertices.size() / 3);
    input.indices = indices.data();
    input.triangle_count = static_cast<int>(indices.size() / 3);
    input.agent = {HEIGHT, RADIUS, CLIMB, SLOPE};
    input.cell_size = CS;
    input.cell_height = CH;
    ck::CookResult first, second;
    if (!expect(ck::cook(input, 64, first) && ck::cook(input, 64, second), "direct cooks succeed")) return 40;
    if (!expect(first.blob.size() == second.blob.size() && first.blob.size() > sizeof(ck::BlobHeader) &&
            std::memcmp(first.blob.data(), second.blob.data(), first.blob.size()) == 0,
            "two cooks are byte-identical")) return 41;
    if (!expect(first.header.tile_count > 1, "direct cook spans tiles")) return 42;
    probe::nav::NavMeshArtifact loaded;
    ck::Diagnostics diag;
    if (!expect(ck::load(first.blob, loaded, diag) && loaded.ready(), "blob loads")) return 43;
    std::vector<unsigned char> corrupt = first.blob;
    corrupt[corrupt.size() / 2] ^= 0x40;
    if (!expect(!ck::load(corrupt, loaded, diag) && !loaded.ready() && diag.has_error() &&
            diag.items.back().code == ck::Code::CacheCorrupt, "a flipped byte is rejected")) return 44;
    std::vector<unsigned char> old_version = first.blob;
    old_version[4] = 9;
    ck::Diagnostics version_diag;
    if (!expect(!ck::load(old_version, loaded, version_diag) &&
            version_diag.items.back().code == ck::Code::CacheVersion, "a new version is rejected")) return 45;
    return 0;
}

int bad_input() {
    uint32_t slot = 0, generation = 0, outcome = 0;
    elisa_navigation_v1_begin();
    if (!expect(cook(90, slot, generation, outcome) == ELISA_NAVIGATION_INVALID_ARGUMENT &&
            has_diagnostic(ck::Code::InvalidInput, ck::Severity::Error), "empty scene diagnoses")) return 50;
    stage_scene(-2.0f);
    if (!expect(cook(91, slot, generation, outcome, RADIUS, 4) == ELISA_NAVIGATION_INVALID_ARGUMENT &&
            has_diagnostic(ck::Code::InvalidInput, ck::Severity::Error), "tiny tiles diagnose")) return 51;
    // A cube standing on a corner (body diagonal up) has every face 54.7 degrees steep.
    elisa_navigation_v1_begin();
    elisa_navigation_v1_add_oriented_box(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 1.0f, -0.32506f, 0.0f, 0.32506f, 0.88807f);
    if (!expect(cook(92, slot, generation, outcome) == ELISA_NAVIGATION_BAKE_FAILED &&
            has_diagnostic(ck::Code::NoWalkableSurface, ck::Severity::Error), "steep scene diagnoses")) return 52;
    // A top narrower than the agent erodes to nothing.
    elisa_navigation_v1_begin();
    box(0.0f, 0.0f, 0.0f, 0.1f, 0.1f, 0.1f);
    if (!expect(cook(93, slot, generation, outcome) == ELISA_NAVIGATION_BAKE_FAILED &&
            has_diagnostic(ck::Code::EmptyMesh, ck::Severity::Error), "eroded scene diagnoses")) return 53;
    elisa_navigation_v1_begin();
    box(0.0f, 0.0f, 0.0f, 9000.0f, 0.1f, 9000.0f);
    if (!expect(cook(94, slot, generation, outcome) == ELISA_NAVIGATION_BAKE_FAILED &&
            has_diagnostic(ck::Code::GridTooLarge, ck::Severity::Error), "huge scene diagnoses")) return 54;
    // A link into the void and a volume over no ground still cook, with warnings.
    elisa_navigation_v1_begin();
    box(0.0f, -0.1f, 0.0f, 4.0f, 0.1f, 4.0f);
    elisa_navigation_v1_mark_area(20.0f, -1.0f, 20.0f, 21.0f, 1.0f, 21.0f, 4);
    elisa_navigation_v1_add_link(0.0f, 0.0f, 0.0f, 30.0f, 0.0f, 30.0f, 0.4f, 0);
    if (!expect(cook(95, slot, generation, outcome) == ELISA_NAVIGATION_OK &&
            has_diagnostic(ck::Code::AreaVolumeUnused, ck::Severity::Warning) &&
            has_diagnostic(ck::Code::LinkEndpointOffMesh, ck::Severity::Warning), "warnings reported")) return 55;
    return elisa_navigation_v1_unload(slot, generation) == ELISA_NAVIGATION_OK ? 0 : 56;
}

} // namespace

int main() {
    if (const int failure = direct_determinism(); failure != 0) return failure;
    uint32_t slot = 0, generation = 0, outcome = 9;
    if (!expect(stage_scene(-2.0f) && cook(1, slot, generation, outcome) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Miss), "first cook misses")) return 1;
    const Info first = info();
    if (!expect(first.tiles > 1 && first.tiles_x * first.tiles_z >= first.tiles && first.polygons > 0 &&
            elisa_navigation_v1_cook_area_polygons(3) > 0, "multi-tile cook with a door area")) return 2;
    if (!expect(elisa_navigation_v1_cook_diagnostic_count() == 0, "clean scene has no diagnostics")) return 3;
    bool mezzanine = false, link = false;
    for (uint32_t i = 0; i < elisa_navigation_v1_overlay_count(); ++i) {
        float ax, ay, az, bx, by, bz;
        uint32_t area = 0;
        int32_t source = 0, is_link = 0;
        elisa_navigation_v1_overlay_line(i, &ax, &ay, &az, &bx, &by, &bz, &area, &source, &is_link);
        mezzanine = mezzanine || (source == static_cast<int32_t>(MEZZANINE_BOX) && ay > 1.3f);
        link = link || (is_link == 1 && area == ELISA_NAVIGATION_AREA_LINK);
    }
    if (!expect(mezzanine && link, "overlay ties polygons to source boxes and shows the link")) return 4;
    int32_t route = 0;
    uint32_t count = 0;
    if (!expect(elisa_navigation_v1_query_path(slot, generation, -4.0f, 0.0f, -3.0f, 15.0f, 1.5f, -3.0f,
            1.0f, 1.0f, 1.0f, &route, &count) == ELISA_NAVIGATION_OK && route == ELISA_NAVIGATION_ROUTE_FOUND,
            "route climbs the stairs through the doorway")) return 5;
    uint32_t hit_slot = 0, hit_generation = 0;
    if (!expect(cook(1, hit_slot, hit_generation, outcome) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Hit) && info().digest == first.digest,
            "same scene and profile hit the cache")) return 6;
    elisa_navigation_v1_cache_clear();
    uint32_t again_slot = 0, again_generation = 0;
    if (!expect(cook(1, again_slot, again_generation, outcome) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Miss) && info().digest == first.digest &&
            info().bytes == first.bytes, "a re-cook reproduces the digest")) return 7;
    uint32_t wide_slot = 0, wide_generation = 0;
    if (!expect(cook(1, wide_slot, wide_generation, outcome, 0.4f) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Invalidated) && info().digest != first.digest,
            "a new agent profile invalidates")) return 8;
    uint32_t moved_slot = 0, moved_generation = 0;
    if (!expect(stage_scene(-1.0f) && cook(1, moved_slot, moved_generation, outcome) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Invalidated), "moved geometry invalidates")) return 9;
    const std::string path = std::string(std::getenv("TMPDIR") ? std::getenv("TMPDIR") : "/tmp") + "/navmesh_cook_test.cache";
    if (!expect(elisa_navigation_v1_cache_save(path.c_str()) == ELISA_NAVIGATION_OK &&
            elisa_navigation_v1_cache_clear() == ELISA_NAVIGATION_OK && elisa_navigation_v1_cache_size() == 0 &&
            elisa_navigation_v1_cache_load(path.c_str()) == ELISA_NAVIGATION_OK &&
            elisa_navigation_v1_cache_size() == 1, "cache round-trips through a file")) return 10;
    uint32_t loaded_slot = 0, loaded_generation = 0;
    if (!expect(cook(1, loaded_slot, loaded_generation, outcome) == ELISA_NAVIGATION_OK &&
            outcome == static_cast<uint32_t>(ck::CacheOutcome::Hit), "loaded cache hits")) return 11;
    std::FILE* file = std::fopen(path.c_str(), "r+b");
    if (!expect(file != nullptr, "reopen cache file")) return 12;
    std::fseek(file, 200, SEEK_SET);
    std::fputc(0x5a, file);
    std::fclose(file);
    if (!expect(elisa_navigation_v1_cache_load(path.c_str()) == ELISA_NAVIGATION_BAKE_FAILED &&
            has_diagnostic(ck::Code::CacheCorrupt, ck::Severity::Error) && elisa_navigation_v1_cache_size() == 1,
            "a corrupt cache file is diagnosed and ignored")) return 13;
    std::remove(path.c_str());
    const uint32_t slots[] = {slot, hit_slot, again_slot, wide_slot, moved_slot, loaded_slot};
    const uint32_t generations[] = {generation, hit_generation, again_generation, wide_generation,
        moved_generation, loaded_generation};
    for (int i = 0; i < 6; ++i) elisa_navigation_v1_unload(slots[i], generations[i]);
    if (!expect(elisa_navigation_v1_live_count() == 0, "every cooked mesh unloads")) return 14;
    if (const int failure = bad_input(); failure != 0) return failure;
    std::cout << "Navmesh cook tests passed.\n";
    return 0;
}
