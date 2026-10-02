// N01: cooked, cached, tiled navmeshes over the staged scene. Included at the
// end of navigation_service_abi.cpp so it shares the service, its lock and its
// store; every entry point takes `nav.mutex`.
#include "navmesh_cook_cache.h"

namespace {

struct TileOverride {
    uint32_t tx = 0;
    uint32_t tz = 0;
    std::vector<unsigned char> data; // empty: the rebuild left no tile
};

struct SlotCook {
    probe::nav::AgentProfile agent{};
    float cell_size = 0.0f;
    float cell_height = 0.0f;
    int tile_cells = 0;
    float top = 0.0f; // the cook's upper bound; rebuilds only raise it
    std::vector<TileOverride> rebuilt;
    uint32_t epoch = 0; // bumped by every tile load, unload and rebuild
    std::vector<uint32_t> tile_epochs; // per tile (tz * tiles_x + tx), sized on first bump
};

struct CookState {
    probe::nav::cook::CookCache cache;
    probe::nav::cook::Diagnostics diagnostics;
    std::vector<probe::nav::cook::OverlayLine> overlay;
    probe::nav::cook::BlobHeader header{};
    uint64_t digest = 0;
    uint32_t bytes = 0;
    // The blob each cooked store slot was built from, for tile streaming.
    std::array<std::vector<unsigned char>, probe::nav::NavMeshTileStore::MAX_TILES> blobs;
    std::array<uint32_t, probe::nav::NavMeshTileStore::MAX_TILES> blob_generation{};
    // What each cooked slot was cooked with, so one tile can be rebuilt.
    std::array<SlotCook, probe::nav::NavMeshTileStore::MAX_TILES> cooks{};
};

CookState& cook_state() {
    static CookState state;
    return state;
}

uint64_t blob_digest(const std::vector<unsigned char>& blob) {
    uint64_t value = 0;
    if (blob.size() >= sizeof value) std::memcpy(&value, blob.data() + blob.size() - sizeof value, sizeof value);
    return value;
}

} // namespace

extern "C" {

int32_t elisa_navigation_v1_cook(float agent_radius, float agent_height, float max_climb,
    float max_slope_degrees, float cell_size, float cell_height, uint32_t tile_cells, uint64_t scene_id,
    uint32_t* slot, uint32_t* generation, uint32_t* cache_outcome) {
    namespace ck = probe::nav::cook;
    if (slot == nullptr || generation == nullptr || cache_outcome == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *slot = probe::nav::TileHandle::INVALID_SLOT;
    *generation = 0;
    *cache_outcome = static_cast<uint32_t>(ck::CacheOutcome::Miss);
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    CookState& state = cook_state();
    state.diagnostics = {};
    state.overlay.clear();
    state.header = {};
    state.digest = 0;
    state.bytes = 0;
    if (!finite(agent_radius) || !positive(agent_height) || !finite(max_climb) || !finite(max_slope_degrees) ||
        !finite(cell_size) || !finite(cell_height) || nav.boxes == 0) {
        state.diagnostics.add(ck::Code::InvalidInput, ck::Severity::Error, nav.boxes == 0 ? 1u : 0u);
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    probe::nav::BakeInput input;
    input.vertices = nav.vertices.data();
    input.vertex_count = static_cast<int>(nav.vertices.size() / 3);
    input.indices = nav.indices.data();
    input.triangle_count = static_cast<int>(nav.indices.size() / 3);
    input.volumes = nav.volumes.data();
    input.volume_count = static_cast<int>(nav.volumes.size());
    input.links = nav.links.data();
    input.link_count = static_cast<int>(nav.links.size());
    input.agent = {agent_height, agent_radius, max_climb, max_slope_degrees};
    input.cell_size = cell_size;
    input.cell_height = cell_height;
    const int cells = tile_cells > static_cast<uint32_t>(ck::MAX_TILE_CELLS) ? 0 : static_cast<int>(tile_cells);
    const uint64_t source = ck::source_digest(input);
    const uint64_t settings = ck::settings_digest(input, cells);
    const std::vector<unsigned char>* cached = nullptr;
    const ck::CacheOutcome outcome = state.cache.lookup(scene_id, source, settings, &cached);
    *cache_outcome = static_cast<uint32_t>(outcome);
    std::vector<unsigned char> blob;
    if (cached != nullptr) {
        blob = *cached;
    } else {
        ck::CookResult result;
        const bool cooked = ck::cook(input, cells, result);
        state.diagnostics = result.diagnostics;
        if (!cooked) {
            return state.diagnostics.items.empty() || state.diagnostics.items.back().code != ck::Code::InvalidInput
                ? ELISA_NAVIGATION_BAKE_FAILED : ELISA_NAVIGATION_INVALID_ARGUMENT;
        }
        blob = std::move(result.blob);
    }
    auto artifact = std::make_unique<probe::nav::NavMeshArtifact>();
    if (!ck::load(blob, *artifact, state.diagnostics, &state.header)) return ELISA_NAVIGATION_BAKE_FAILED;
    if (cached == nullptr) {
        ck::check_links(input, *artifact, state.diagnostics);
        state.cache.store(scene_id, state.header, blob);
    }
    ck::overlay(input, *artifact, state.overlay);
    const probe::nav::TileHandle handle = nav.store.publish(std::move(artifact));
    if (handle.slot == probe::nav::TileHandle::INVALID_SLOT) return ELISA_NAVIGATION_CAPACITY;
    nav.filters[handle.slot] = probe::nav::QueryFilter();
    state.blobs[handle.slot] = blob;
    state.blob_generation[handle.slot] = handle.generation;
    float low[3], high[3];
    rcCalcBounds(input.vertices, input.vertex_count, low, high);
    state.cooks[handle.slot] = {input.agent, cell_size, cell_height, cells, high[1] + agent_height, {}, 0, {}};
    state.digest = blob_digest(blob);
    state.bytes = static_cast<uint32_t>(blob.size());
    *slot = handle.slot;
    *generation = handle.generation;
    return ELISA_NAVIGATION_OK;
}

uint32_t elisa_navigation_v1_cook_diagnostic_count() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return static_cast<uint32_t>(cook_state().diagnostics.items.size());
}

int32_t elisa_navigation_v1_cook_diagnostic(uint32_t index, uint32_t* code, uint32_t* severity,
    uint32_t* subject, uint32_t* count) {
    if (code == nullptr || severity == nullptr || subject == nullptr || count == nullptr) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const auto& items = cook_state().diagnostics.items;
    if (index >= items.size()) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    *code = static_cast<uint32_t>(items[index].code);
    *severity = static_cast<uint32_t>(items[index].severity);
    *subject = items[index].subject;
    *count = items[index].count;
    return ELISA_NAVIGATION_OK;
}

// The last successful cook: tile grid, tiles kept, polygons, blob size and digest.
int32_t elisa_navigation_v1_cook_info(uint32_t* tiles_x, uint32_t* tiles_z, uint32_t* tiles,
    uint32_t* polygons, uint32_t* bytes, uint64_t* digest) {
    if (tiles_x == nullptr || tiles_z == nullptr || tiles == nullptr || polygons == nullptr ||
        bytes == nullptr || digest == nullptr) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const CookState& state = cook_state();
    *tiles_x = state.header.tiles_x;
    *tiles_z = state.header.tiles_z;
    *tiles = state.header.tile_count;
    *polygons = state.header.polygon_count;
    *bytes = state.bytes;
    *digest = state.digest;
    return state.bytes == 0 ? ELISA_NAVIGATION_INVALID_ARGUMENT : ELISA_NAVIGATION_OK;
}

// Ground polygons of `area` in the last successful cook.
uint32_t elisa_navigation_v1_cook_area_polygons(uint32_t area) {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return area < probe::nav::FLAGGED_AREAS ? cook_state().header.area_polys[area] : 0;
}

uint32_t elisa_navigation_v1_overlay_count() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return static_cast<uint32_t>(cook_state().overlay.size());
}

// One overlay segment. `source_box` is the staged box whose top the polygon
// came from, or -1; `link` is 1 for an off-mesh link segment.
int32_t elisa_navigation_v1_overlay_line(uint32_t index, float* ax, float* ay, float* az,
    float* bx, float* by, float* bz, uint32_t* area, int32_t* source_box, int32_t* link) {
    if (ax == nullptr || ay == nullptr || az == nullptr || bx == nullptr || by == nullptr ||
        bz == nullptr || area == nullptr || source_box == nullptr || link == nullptr) {
        return ELISA_NAVIGATION_INVALID_ARGUMENT;
    }
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    const auto& lines = cook_state().overlay;
    if (index >= lines.size()) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    const probe::nav::cook::OverlayLine& line = lines[index];
    *ax = line.a[0]; *ay = line.a[1]; *az = line.a[2];
    *bx = line.b[0]; *by = line.b[1]; *bz = line.b[2];
    *area = line.area;
    *source_box = line.source_triangle < 0 ? -1 : line.source_triangle / static_cast<int32_t>(BOX_TRIANGLES);
    *link = line.link ? 1 : 0;
    return ELISA_NAVIGATION_OK;
}

uint32_t elisa_navigation_v1_cache_size() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    return static_cast<uint32_t>(cook_state().cache.size());
}

int32_t elisa_navigation_v1_cache_clear() {
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    cook_state().cache.clear();
    return ELISA_NAVIGATION_OK;
}

int32_t elisa_navigation_v1_cache_save(const char* path) {
    if (path == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    CookState& state = cook_state();
    state.diagnostics = {};
    return state.cache.save(path, state.diagnostics) ? ELISA_NAVIGATION_OK : ELISA_NAVIGATION_BAKE_FAILED;
}

// Replace the cache with a saved one. A missing, corrupt or old-version file
// leaves the cache unchanged and reports a CacheCorrupt or CacheVersion diagnostic.
int32_t elisa_navigation_v1_cache_load(const char* path) {
    if (path == nullptr) return ELISA_NAVIGATION_INVALID_ARGUMENT;
    Service& nav = service();
    std::lock_guard<std::mutex> lock(nav.mutex);
    CookState& state = cook_state();
    state.diagnostics = {};
    return state.cache.load_file(path, state.diagnostics) ? ELISA_NAVIGATION_OK : ELISA_NAVIGATION_BAKE_FAILED;
}

}
