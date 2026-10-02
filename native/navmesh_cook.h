#pragma once

// N01: deterministic tiled Recast cook. One staged scene and one agent profile
// cook into a self-describing blob ('ENAV'): a header with the source and
// settings digests, an area histogram and per-tile records (grid position,
// polygon count, area mask, bounds, digest) ahead of each Detour tile. Bad
// input never throws or half-publishes; it yields bounded diagnostics.
#include "navmesh_service.h"
#include "DetourCommon.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace probe::nav::cook {

constexpr uint32_t BLOB_MAGIC = 0x56414e45u; // "ENAV"
constexpr uint32_t BLOB_VERSION = 1;
constexpr int MAX_COOK_TILES = 64;
constexpr int MAX_TILE_POLYS = 4096;
constexpr int MIN_TILE_CELLS = 16;
constexpr int MAX_TILE_CELLS = 512;
constexpr int MAX_DIAGNOSTICS = 32;

enum class Code : uint32_t {
    InvalidInput = 1,
    GridTooLarge = 2,
    NoWalkableSurface = 3,
    DegenerateTriangle = 4,
    EmptyMesh = 5,
    LinkEndpointOffMesh = 6,
    AreaVolumeUnused = 7,
    TileCapacity = 8,
    CacheCorrupt = 9,
    CacheVersion = 10,
};

enum class Severity : uint32_t { Warning = 1, Error = 2 };

// `subject` names the triangle, volume, link or tile concerned; `count` how many share the code.
struct Diagnostic {
    Code code = Code::InvalidInput;
    Severity severity = Severity::Error;
    uint32_t subject = 0;
    uint32_t count = 1;
};

struct Diagnostics {
    std::vector<Diagnostic> items;
    void add(Code code, Severity severity, uint32_t subject, uint32_t count = 1) {
        if (items.size() < static_cast<size_t>(MAX_DIAGNOSTICS)) items.push_back({code, severity, subject, count});
    }
    bool has_error() const {
        for (const Diagnostic& item : items) if (item.severity == Severity::Error) return true;
        return false;
    }
};

// FNV-1a 64 over explicit fields, never over padded structs.
struct Digest {
    uint64_t value = 1469598103934665603ull;
    void bytes(const void* data, size_t size) {
        const unsigned char* p = static_cast<const unsigned char*>(data);
        for (size_t i = 0; i < size; ++i) { value ^= p[i]; value *= 1099511628211ull; }
    }
    void f32(float v) { bytes(&v, sizeof v); }
    void u32(uint32_t v) { bytes(&v, sizeof v); }
};

inline uint64_t source_digest(const BakeInput& input) {
    Digest d;
    d.u32(static_cast<uint32_t>(input.vertex_count));
    d.bytes(input.vertices, sizeof(float) * 3 * static_cast<size_t>(input.vertex_count));
    d.u32(static_cast<uint32_t>(input.triangle_count));
    d.bytes(input.indices, sizeof(int) * 3 * static_cast<size_t>(input.triangle_count));
    if (input.areas != nullptr) d.bytes(input.areas, static_cast<size_t>(input.triangle_count));
    d.u32(static_cast<uint32_t>(input.volume_count));
    for (int i = 0; i < input.volume_count; ++i) {
        for (int a = 0; a < 3; ++a) { d.f32(input.volumes[i].min[a]); d.f32(input.volumes[i].max[a]); }
        d.u32(input.volumes[i].area);
    }
    d.u32(static_cast<uint32_t>(input.link_count));
    for (int i = 0; i < input.link_count; ++i) {
        for (int a = 0; a < 3; ++a) { d.f32(input.links[i].start[a]); d.f32(input.links[i].end[a]); }
        d.f32(input.links[i].radius);
        d.u32(input.links[i].bidirectional ? 1u : 0u);
    }
    return d.value;
}

inline uint64_t settings_digest(const BakeInput& input, int tile_cells) {
    Digest d;
    d.u32(BLOB_VERSION);
    d.f32(input.agent.height); d.f32(input.agent.radius);
    d.f32(input.agent.climb); d.f32(input.agent.slope);
    d.f32(input.cell_size); d.f32(input.cell_height);
    d.u32(static_cast<uint32_t>(tile_cells));
    return d.value;
}

struct BlobHeader {
    uint32_t magic = BLOB_MAGIC;
    uint32_t version = BLOB_VERSION;
    uint64_t source = 0;
    uint64_t settings = 0;
    float origin[3]{};
    float tile_world = 0.0f;
    uint32_t tiles_x = 0;
    uint32_t tiles_z = 0;
    uint32_t tile_count = 0;
    uint32_t polygon_count = 0;
    uint32_t area_polys[FLAGGED_AREAS]{};
    float walkable_height = 0.0f;
    float walkable_radius = 0.0f;
    float walkable_climb = 0.0f;
    uint32_t reserved = 0;
};

struct TileRecord {
    uint32_t tx = 0;
    uint32_t tz = 0;
    uint32_t polygons = 0;
    uint32_t size = 0;
    uint32_t area_mask = 0;
    float bmin[3]{};
    float bmax[3]{};
    uint32_t reserved = 0;
    uint64_t digest = 0;
};
static_assert(sizeof(BlobHeader) == 136 && sizeof(TileRecord) == 56, "packed cook records");

template <typename T> void put(std::vector<unsigned char>& out, const T& value) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(&value);
    out.insert(out.end(), p, p + sizeof(T));
}

template <typename T> bool get(const std::vector<unsigned char>& in, size_t& at, T& value) {
    if (at > in.size() || in.size() - at < sizeof(T)) return false;
    std::memcpy(&value, in.data() + at, sizeof(T));
    at += sizeof(T);
    return true;
}

struct CookResult {
    std::vector<unsigned char> blob;
    Diagnostics diagnostics;
    BlobHeader header;
    uint64_t digest = 0; // trailing digest of the blob
};

// Recast config shared by every tile of one cook.
inline rcConfig tile_config(const BakeInput& input, int tile_cells) {
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
    config.tileSize = tile_cells;
    config.borderSize = config.walkableRadius + 3;
    config.width = tile_cells + config.borderSize * 2;
    config.height = tile_cells + config.borderSize * 2;
    return config;
}

struct RecastScratch {
    rcHeightfield* heightfield = rcAllocHeightfield();
    rcCompactHeightfield* compact = rcAllocCompactHeightfield();
    rcContourSet* contours = rcAllocContourSet();
    rcPolyMesh* poly = rcAllocPolyMesh();
    rcPolyMeshDetail* detail = rcAllocPolyMeshDetail();
    RecastScratch() = default;
    RecastScratch(const RecastScratch&) = delete;
    RecastScratch& operator=(const RecastScratch&) = delete;
    ~RecastScratch() {
        rcFreeHeightField(heightfield); rcFreeCompactHeightfield(compact);
        rcFreeContourSet(contours); rcFreePolyMesh(poly); rcFreePolyMeshDetail(detail);
    }
    bool ok() const { return heightfield && compact && contours && poly && detail; }
};

// Build one tile. Returns false on a Recast failure; an empty tile succeeds with no data.
inline bool build_tile(const BakeInput& input, const std::vector<unsigned char>& areas, rcConfig config,
    const float origin[3], float top, int tx, int tz, std::vector<unsigned char>& data, int& polygons) {
    data.clear();
    polygons = 0;
    const float tile_world = config.tileSize * config.cs;
    const float border = config.borderSize * config.cs;
    config.bmin[0] = origin[0] + tx * tile_world - border;
    config.bmin[1] = origin[1];
    config.bmin[2] = origin[2] + tz * tile_world - border;
    config.bmax[0] = origin[0] + (tx + 1) * tile_world + border;
    config.bmax[1] = top;
    config.bmax[2] = origin[2] + (tz + 1) * tile_world + border;
    RecastScratch s;
    if (!s.ok()) return false;
    rcContext context(false);
    if (!rcCreateHeightfield(&context, *s.heightfield, config.width, config.height,
            config.bmin, config.bmax, config.cs, config.ch) ||
        !rcRasterizeTriangles(&context, input.vertices, input.vertex_count, input.indices,
            areas.data(), input.triangle_count, *s.heightfield, config.walkableClimb)) {
        return false;
    }
    rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *s.heightfield);
    rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *s.heightfield);
    rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *s.heightfield);
    if (!rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb,
            *s.heightfield, *s.compact) || !rcErodeWalkableArea(&context, config.walkableRadius, *s.compact)) {
        return false;
    }
    for (int i = 0; i < input.volume_count; ++i) {
        rcMarkBoxArea(&context, input.volumes[i].min, input.volumes[i].max, input.volumes[i].area, *s.compact);
    }
    if (s.compact->spanCount == 0) return true;
    if (!rcBuildDistanceField(&context, *s.compact) ||
        !rcBuildRegions(&context, *s.compact, config.borderSize, config.minRegionArea, config.mergeRegionArea) ||
        !rcBuildContours(&context, *s.compact, config.maxSimplificationError, config.maxEdgeLen, *s.contours)) {
        return false;
    }
    if (s.contours->nconts == 0) return true;
    if (!rcBuildPolyMesh(&context, *s.contours, config.maxVertsPerPoly, *s.poly) ||
        !rcBuildPolyMeshDetail(&context, *s.poly, *s.compact, config.detailSampleDist,
            config.detailSampleMaxError, *s.detail)) {
        return false;
    }
    if (s.poly->npolys == 0) return true;
    for (int i = 0; i < s.poly->npolys; ++i) {
        if (s.poly->areas[i] == RC_WALKABLE_AREA) s.poly->areas[i] = WALK_AREA;
        s.poly->flags[i] = area_flags(s.poly->areas[i]);
    }
    // Every link goes to every tile; Detour keeps those that start inside it.
    const int links = input.link_count;
    std::vector<float> link_vertices(links * 6), link_radii(links);
    std::vector<unsigned char> link_directions(links), link_areas(links, LINK_AREA);
    std::vector<unsigned short> link_flags(links, area_flags(LINK_AREA));
    std::vector<unsigned int> link_ids(links);
    for (int i = 0; i < links; ++i) {
        std::copy(input.links[i].start, input.links[i].start + 3, link_vertices.begin() + i * 6);
        std::copy(input.links[i].end, input.links[i].end + 3, link_vertices.begin() + i * 6 + 3);
        link_radii[i] = input.links[i].radius;
        link_directions[i] = input.links[i].bidirectional ? DT_OFFMESH_CON_BIDIR : 0;
        link_ids[i] = static_cast<unsigned int>(i + 1);
    }
    dtNavMeshCreateParams params{};
    params.verts = s.poly->verts; params.vertCount = s.poly->nverts;
    params.polys = s.poly->polys; params.polyAreas = s.poly->areas;
    params.polyFlags = s.poly->flags; params.polyCount = s.poly->npolys;
    params.nvp = s.poly->nvp; params.detailMeshes = s.detail->meshes;
    params.detailVerts = s.detail->verts; params.detailVertsCount = s.detail->nverts;
    params.detailTris = s.detail->tris; params.detailTriCount = s.detail->ntris;
    params.offMeshConVerts = link_vertices.data(); params.offMeshConRad = link_radii.data();
    params.offMeshConDir = link_directions.data(); params.offMeshConAreas = link_areas.data();
    params.offMeshConFlags = link_flags.data(); params.offMeshConUserID = link_ids.data();
    params.offMeshConCount = links;
    params.walkableHeight = input.agent.height; params.walkableRadius = input.agent.radius;
    params.walkableClimb = input.agent.climb;
    params.tileX = tx; params.tileY = tz; params.tileLayer = 0;
    rcVcopy(params.bmin, s.poly->bmin); rcVcopy(params.bmax, s.poly->bmax);
    params.cs = config.cs; params.ch = config.ch; params.buildBvTree = true;
    unsigned char* nav_data = nullptr;
    int size = 0;
    if (!dtCreateNavMeshData(&params, &nav_data, &size) || nav_data == nullptr || size <= 0) return false;
    data.assign(nav_data, nav_data + size);
    dtFree(nav_data);
    polygons = s.poly->npolys;
    return true;
}

// Cook `input` into `result.blob`. Fails, with at least one error diagnostic
// and an empty blob, on invalid input, an oversized grid, no walkable surface,
// a Recast failure or too many tiles or polygons.
inline bool cook(const BakeInput& input, int tile_cells, CookResult& result) {
    result = {};
    Diagnostics& diag = result.diagnostics;
    std::string error;
    if (tile_cells < MIN_TILE_CELLS || tile_cells > MAX_TILE_CELLS || !valid_input(input, error)) {
        diag.add(Code::InvalidInput, Severity::Error, 0);
        return false;
    }
    uint32_t degenerate = 0, first_degenerate = 0;
    for (int t = 0; t < input.triangle_count; ++t) {
        const float* a = input.vertices + input.indices[t * 3] * 3;
        const float* b = input.vertices + input.indices[t * 3 + 1] * 3;
        const float* c = input.vertices + input.indices[t * 3 + 2] * 3;
        const float e0[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
        const float e1[3] = {c[0] - a[0], c[1] - a[1], c[2] - a[2]};
        float n[3];
        rcVcross(n, e0, e1);
        if (rcVdot(n, n) <= 1.0e-12f) { if (degenerate++ == 0) first_degenerate = static_cast<uint32_t>(t); }
    }
    if (degenerate > 0) diag.add(Code::DegenerateTriangle, Severity::Warning, first_degenerate, degenerate);
    std::vector<unsigned char> areas(input.triangle_count, 0);
    if (input.areas != nullptr) {
        areas.assign(input.areas, input.areas + input.triangle_count);
    } else {
        rcContext context(false);
        rcMarkWalkableTriangles(&context, input.agent.slope, input.vertices, input.vertex_count,
            input.indices, input.triangle_count, areas.data());
    }
    bool walkable = false;
    for (unsigned char area : areas) walkable = walkable || area != RC_NULL_AREA;
    if (!walkable) {
        diag.add(Code::NoWalkableSurface, Severity::Error, 0, static_cast<uint32_t>(input.triangle_count));
        return false;
    }
    float bmin[3], bmax[3];
    rcCalcBounds(input.vertices, input.vertex_count, bmin, bmax);
    bmin[0] -= 1.0f; bmin[1] -= 1.0f; bmin[2] -= 1.0f;
    bmax[0] += 1.0f; bmax[1] += input.agent.height; bmax[2] += 1.0f;
    const double cells_x = std::ceil((static_cast<double>(bmax[0]) - bmin[0]) / input.cell_size);
    const double cells_z = std::ceil((static_cast<double>(bmax[2]) - bmin[2]) / input.cell_size);
    if (!(cells_x * cells_z <= static_cast<double>(MAX_GRID_CELLS)) ||
        cells_x > MAX_GRID_DIMENSION || cells_z > MAX_GRID_DIMENSION) {
        diag.add(Code::GridTooLarge, Severity::Error, 0);
        return false;
    }
    const int tiles_x = static_cast<int>((cells_x + tile_cells - 1) / tile_cells);
    const int tiles_z = static_cast<int>((cells_z + tile_cells - 1) / tile_cells);
    if (tiles_x * tiles_z > MAX_COOK_TILES) {
        diag.add(Code::TileCapacity, Severity::Error, 0, static_cast<uint32_t>(tiles_x * tiles_z));
        return false;
    }
    const rcConfig config = tile_config(input, tile_cells);
    BlobHeader& header = result.header;
    header.source = source_digest(input);
    header.settings = settings_digest(input, tile_cells);
    rcVcopy(header.origin, bmin);
    header.tile_world = tile_cells * input.cell_size;
    header.tiles_x = static_cast<uint32_t>(tiles_x);
    header.tiles_z = static_cast<uint32_t>(tiles_z);
    header.walkable_height = input.agent.height;
    header.walkable_radius = input.agent.radius;
    header.walkable_climb = input.agent.climb;
    std::vector<TileRecord> records;
    std::vector<std::vector<unsigned char>> tiles;
    std::vector<unsigned char> data;
    for (int tz = 0; tz < tiles_z; ++tz) {
        for (int tx = 0; tx < tiles_x; ++tx) {
            int polygons = 0;
            if (!build_tile(input, areas, config, bmin, bmax[1], tx, tz, data, polygons)) {
                diag.add(Code::InvalidInput, Severity::Error, static_cast<uint32_t>(tz * tiles_x + tx));
                return false;
            }
            if (data.empty()) continue;
            if (polygons > MAX_TILE_POLYS - MAX_OFFMESH_LINKS) {
                diag.add(Code::TileCapacity, Severity::Error, static_cast<uint32_t>(tz * tiles_x + tx),
                    static_cast<uint32_t>(polygons));
                return false;
            }
            TileRecord record;
            record.tx = static_cast<uint32_t>(tx);
            record.tz = static_cast<uint32_t>(tz);
            record.polygons = static_cast<uint32_t>(polygons);
            record.size = static_cast<uint32_t>(data.size());
            const dtMeshHeader* tile_header = reinterpret_cast<const dtMeshHeader*>(data.data());
            rcVcopy(record.bmin, tile_header->bmin);
            rcVcopy(record.bmax, tile_header->bmax);
            // Areas come from the tile's polygon array, after the header and vertices.
            const size_t poly_offset = dtAlign4(sizeof(dtMeshHeader)) + dtAlign4(sizeof(float) * 3 * tile_header->vertCount);
            const dtPoly* polys = reinterpret_cast<const dtPoly*>(data.data() + poly_offset);
            for (int p = 0; p < tile_header->polyCount; ++p) {
                if (polys[p].getType() != DT_POLYTYPE_GROUND) continue;
                const unsigned char area = polys[p].getArea();
                if (area < FLAGGED_AREAS) { record.area_mask |= 1u << area; ++header.area_polys[area]; }
            }
            Digest d;
            d.bytes(data.data(), data.size());
            record.digest = d.value;
            header.polygon_count += record.polygons;
            records.push_back(record);
            tiles.push_back(data);
        }
    }
    if (records.empty()) {
        diag.add(Code::EmptyMesh, Severity::Error, 0);
        return false;
    }
    for (int i = 0; i < input.volume_count; ++i) {
        const unsigned char area = input.volumes[i].area;
        if (area >= FLAGGED_AREAS || header.area_polys[area] == 0) {
            diag.add(Code::AreaVolumeUnused, Severity::Warning, static_cast<uint32_t>(i));
        }
    }
    header.tile_count = static_cast<uint32_t>(records.size());
    put(result.blob, header);
    for (size_t i = 0; i < records.size(); ++i) {
        put(result.blob, records[i]);
        result.blob.insert(result.blob.end(), tiles[i].begin(), tiles[i].end());
    }
    Digest whole;
    whole.bytes(result.blob.data(), result.blob.size());
    result.digest = whole.value;
    put(result.blob, result.digest);
    return true;
}

// Validate a blob and build a multi-tile Detour mesh from it. Every check
// runs before any tile is added, so a corrupt blob never yields a partial mesh.
inline bool load(const std::vector<unsigned char>& blob, NavMeshArtifact& artifact, Diagnostics& diag,
    BlobHeader* header_out = nullptr) {
    artifact.reset();
    BlobHeader header;
    size_t at = 0;
    if (!get(blob, at, header) || header.magic != BLOB_MAGIC) {
        diag.add(Code::CacheCorrupt, Severity::Error, 0);
        return false;
    }
    if (header.version != BLOB_VERSION) {
        diag.add(Code::CacheVersion, Severity::Error, header.version);
        return false;
    }
    if (blob.size() < sizeof(BlobHeader) + sizeof(uint64_t)) {
        diag.add(Code::CacheCorrupt, Severity::Error, 0);
        return false;
    }
    Digest whole;
    whole.bytes(blob.data(), blob.size() - sizeof(uint64_t));
    uint64_t trailing = 0;
    size_t tail = blob.size() - sizeof(uint64_t);
    get(blob, tail, trailing);
    if (trailing != whole.value || header.tile_count == 0 || header.tile_count > MAX_COOK_TILES ||
        header.tiles_x == 0 || header.tiles_z == 0 || header.tiles_x * header.tiles_z > MAX_COOK_TILES ||
        !(header.tile_world > 0.0f) || !std::isfinite(header.tile_world)) {
        diag.add(Code::CacheCorrupt, Severity::Error, 1);
        return false;
    }
    std::vector<std::pair<TileRecord, size_t>> tiles;
    for (uint32_t i = 0; i < header.tile_count; ++i) {
        TileRecord record;
        if (!get(blob, at, record) || record.size == 0 || record.size > blob.size() - sizeof(uint64_t) - at ||
            record.tx >= header.tiles_x || record.tz >= header.tiles_z || record.polygons > MAX_TILE_POLYS) {
            diag.add(Code::CacheCorrupt, Severity::Error, 2 + i);
            return false;
        }
        Digest d;
        d.bytes(blob.data() + at, record.size);
        if (d.value != record.digest) {
            diag.add(Code::CacheCorrupt, Severity::Error, 2 + i);
            return false;
        }
        tiles.push_back({record, at});
        at += record.size;
    }
    if (at != blob.size() - sizeof(uint64_t)) {
        diag.add(Code::CacheCorrupt, Severity::Error, 1);
        return false;
    }
    dtNavMeshParams params{};
    rcVcopy(params.orig, header.origin);
    params.tileWidth = header.tile_world;
    params.tileHeight = header.tile_world;
    params.maxTiles = MAX_COOK_TILES;
    params.maxPolys = MAX_TILE_POLYS;
    dtNavMesh* mesh = dtAllocNavMesh();
    dtNavMeshQuery* query = dtAllocNavMeshQuery();
    bool ok = mesh != nullptr && query != nullptr && dtStatusSucceed(mesh->init(&params));
    for (size_t i = 0; ok && i < tiles.size(); ++i) {
        unsigned char* copy = static_cast<unsigned char*>(dtAlloc(tiles[i].first.size, DT_ALLOC_PERM));
        if (copy == nullptr) { ok = false; break; }
        std::memcpy(copy, blob.data() + tiles[i].second, tiles[i].first.size);
        if (dtStatusFailed(mesh->addTile(copy, static_cast<int>(tiles[i].first.size), DT_TILE_FREE_DATA, 0, nullptr))) {
            dtFree(copy);
            ok = false;
        }
    }
    ok = ok && dtStatusSucceed(query->init(mesh, MAX_QUERY_NODES));
    if (!ok) {
        if (query) dtFreeNavMeshQuery(query);
        if (mesh) dtFreeNavMesh(mesh);
        diag.add(Code::CacheCorrupt, Severity::Error, 1);
        return false;
    }
    BakeMetadata metadata;
    metadata.source_generation = static_cast<uint32_t>(header.source);
    metadata.settings_generation = static_cast<uint32_t>(header.settings);
    metadata.polygon_count = static_cast<int>(header.polygon_count);
    metadata.nav_data_size = static_cast<int>(blob.size());
    metadata.width = static_cast<int>(header.tiles_x);
    metadata.height = static_cast<int>(header.tiles_z);
    artifact.adopt(mesh, query, metadata);
    if (header_out != nullptr) *header_out = header;
    return true;
}

} // namespace probe::nav::cook
