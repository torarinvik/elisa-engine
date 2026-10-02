#pragma once

// N01: the cooked-navmesh cache, bake diagnostics that need a loaded mesh,
// and debug overlay lines tied back to the source triangle under each polygon.
#include "navmesh_cook.h"

#include "DetourCommon.h"

#include <cstdio>

namespace probe::nav::cook {

constexpr uint32_t CACHE_MAGIC = 0x4343414eu; // "NACC"
constexpr uint32_t CACHE_VERSION = 1;
constexpr size_t MAX_CACHE_ENTRIES = 8;
constexpr size_t MAX_CACHE_BYTES = 64u * 1024u * 1024u;
constexpr size_t MAX_OVERLAY_LINES = 32768;

enum class CacheOutcome : uint32_t { Miss = 0, Hit = 1, Invalidated = 2 };

// One cooked scene per caller-chosen id. A lookup hits only when both the
// source and the settings digest match; a scene whose digests changed is
// invalidated and its old blob dropped.
class CookCache {
public:
    struct Entry {
        uint64_t scene = 0;
        uint64_t source = 0;
        uint64_t settings = 0;
        std::vector<unsigned char> blob;
    };

    CacheOutcome lookup(uint64_t scene, uint64_t source, uint64_t settings, const std::vector<unsigned char>** blob) {
        *blob = nullptr;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].scene != scene) continue;
            if (entries_[i].source == source && entries_[i].settings == settings) {
                *blob = &entries_[i].blob;
                return CacheOutcome::Hit;
            }
            entries_.erase(entries_.begin() + static_cast<long>(i));
            return CacheOutcome::Invalidated;
        }
        return CacheOutcome::Miss;
    }

    void store(uint64_t scene, const BlobHeader& header, const std::vector<unsigned char>& blob) {
        for (Entry& entry : entries_) {
            if (entry.scene == scene) { entry = {scene, header.source, header.settings, blob}; return; }
        }
        if (entries_.size() == MAX_CACHE_ENTRIES) entries_.erase(entries_.begin());
        entries_.push_back({scene, header.source, header.settings, blob});
    }

    void clear() { entries_.clear(); }
    size_t size() const { return entries_.size(); }

    bool save(const char* path, Diagnostics& diag) const {
        std::vector<unsigned char> out;
        put(out, CACHE_MAGIC);
        put(out, CACHE_VERSION);
        put(out, static_cast<uint32_t>(entries_.size()));
        for (const Entry& entry : entries_) {
            put(out, entry.scene);
            put(out, static_cast<uint64_t>(entry.blob.size()));
            out.insert(out.end(), entry.blob.begin(), entry.blob.end());
        }
        Digest d;
        d.bytes(out.data(), out.size());
        put(out, d.value);
        std::FILE* file = path != nullptr ? std::fopen(path, "wb") : nullptr;
        bool ok = file != nullptr && std::fwrite(out.data(), 1, out.size(), file) == out.size();
        if (file != nullptr) ok = std::fclose(file) == 0 && ok;
        if (!ok) diag.add(Code::CacheCorrupt, Severity::Error, 0);
        return ok;
    }

    // Replace the cache with a file's entries only when every entry validates.
    bool load_file(const char* path, Diagnostics& diag) {
        std::vector<unsigned char> in;
        std::FILE* file = path != nullptr ? std::fopen(path, "rb") : nullptr;
        if (file != nullptr) {
            unsigned char buffer[65536];
            size_t got = 0;
            while ((got = std::fread(buffer, 1, sizeof buffer, file)) > 0 && in.size() <= MAX_CACHE_BYTES) {
                in.insert(in.end(), buffer, buffer + got);
            }
            std::fclose(file);
        }
        size_t at = 0;
        uint32_t magic = 0, version = 0, count = 0;
        if (file == nullptr || in.size() > MAX_CACHE_BYTES || !get(in, at, magic) || magic != CACHE_MAGIC ||
            !get(in, at, version)) {
            diag.add(Code::CacheCorrupt, Severity::Error, 0);
            return false;
        }
        if (version != CACHE_VERSION) {
            diag.add(Code::CacheVersion, Severity::Error, version);
            return false;
        }
        Digest d;
        d.bytes(in.data(), in.size() >= sizeof(uint64_t) ? in.size() - sizeof(uint64_t) : 0);
        uint64_t trailing = 0;
        size_t tail = in.size() >= sizeof(uint64_t) ? in.size() - sizeof(uint64_t) : in.size();
        if (!get(in, at, count) || count > MAX_CACHE_ENTRIES || !get(in, tail, trailing) || trailing != d.value) {
            diag.add(Code::CacheCorrupt, Severity::Error, 1);
            return false;
        }
        std::vector<Entry> loaded;
        for (uint32_t i = 0; i < count; ++i) {
            Entry entry;
            uint64_t size = 0;
            if (!get(in, at, entry.scene) || !get(in, at, size) || size > in.size() - sizeof(uint64_t) - at) {
                diag.add(Code::CacheCorrupt, Severity::Error, 2 + i);
                return false;
            }
            entry.blob.assign(in.begin() + static_cast<long>(at), in.begin() + static_cast<long>(at + size));
            at += size;
            NavMeshArtifact probe;
            BlobHeader header;
            if (!load(entry.blob, probe, diag, &header)) return false;
            entry.source = header.source;
            entry.settings = header.settings;
            loaded.push_back(std::move(entry));
        }
        if (at != in.size() - sizeof(uint64_t)) {
            diag.add(Code::CacheCorrupt, Severity::Error, 1);
            return false;
        }
        entries_ = std::move(loaded);
        return true;
    }

private:
    std::vector<Entry> entries_;
};

// Warn about each link endpoint Detour could not attach to a polygon.
inline void check_links(const BakeInput& input, const NavMeshArtifact& artifact, Diagnostics& diag) {
    for (int i = 0; i < input.link_count; ++i) {
        const OffMeshLink& link = input.links[i];
        const float extents[3] = {link.radius, std::max(input.agent.climb, input.cell_height), link.radius};
        const NearestResult start = artifact.nearest_point(link.start, extents);
        const NearestResult end = artifact.nearest_point(link.end, extents);
        if (start.status != QueryStatus::Success || end.status != QueryStatus::Success) {
            diag.add(Code::LinkEndpointOffMesh, Severity::Warning, static_cast<uint32_t>(i));
        }
    }
}

struct OverlayLine {
    float a[3]{};
    float b[3]{};
    uint32_t area = 0;
    // The source triangle under the polygon's centre, or -1 for links and
    // polygons over no walkable source triangle.
    int32_t source_triangle = -1;
    bool link = false;
};

// The walkable source triangle whose surface lies closest in height to `p`
// among those containing it in XZ.
inline int32_t source_under(const BakeInput& input, const std::vector<unsigned char>& walkable, const float p[3]) {
    int32_t best = -1;
    float best_gap = 0.0f;
    for (int t = 0; t < input.triangle_count; ++t) {
        if (walkable[t] == RC_NULL_AREA) continue;
        const float* a = input.vertices + input.indices[t * 3] * 3;
        const float* b = input.vertices + input.indices[t * 3 + 1] * 3;
        const float* c = input.vertices + input.indices[t * 3 + 2] * 3;
        float height = 0.0f;
        if (!dtClosestHeightPointTriangle(p, a, b, c, height)) continue;
        const float gap = std::fabs(height - p[1]);
        if (best < 0 || gap < best_gap) { best = t; best_gap = gap; }
    }
    return best;
}

// Polygon outlines and link segments of a loaded mesh, in tile order.
inline void overlay(const BakeInput& input, const NavMeshArtifact& artifact, std::vector<OverlayLine>& lines) {
    lines.clear();
    const dtNavMesh* mesh = artifact.mesh();
    if (mesh == nullptr) return;
    std::vector<unsigned char> walkable(input.triangle_count, 0);
    if (input.areas != nullptr) {
        walkable.assign(input.areas, input.areas + input.triangle_count);
    } else {
        rcContext context(false);
        rcMarkWalkableTriangles(&context, input.agent.slope, input.vertices, input.vertex_count,
            input.indices, input.triangle_count, walkable.data());
    }
    for (int i = 0; i < mesh->getMaxTiles(); ++i) {
        const dtMeshTile* tile = mesh->getTile(i);
        if (tile == nullptr || tile->header == nullptr) continue;
        for (int p = 0; p < tile->header->polyCount; ++p) {
            const dtPoly& poly = tile->polys[p];
            if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) {
                if (lines.size() == MAX_OVERLAY_LINES) return;
                OverlayLine line;
                rcVcopy(line.a, &tile->verts[poly.verts[0] * 3]);
                rcVcopy(line.b, &tile->verts[poly.verts[1] * 3]);
                line.area = poly.getArea();
                line.link = true;
                lines.push_back(line);
                continue;
            }
            float centre[3] = {0.0f, 0.0f, 0.0f};
            for (int v = 0; v < poly.vertCount; ++v) rcVadd(centre, centre, &tile->verts[poly.verts[v] * 3]);
            dtVscale(centre, centre, 1.0f / static_cast<float>(poly.vertCount));
            const int32_t source = source_under(input, walkable, centre);
            for (int v = 0; v < poly.vertCount; ++v) {
                if (lines.size() == MAX_OVERLAY_LINES) return;
                OverlayLine line;
                rcVcopy(line.a, &tile->verts[poly.verts[v] * 3]);
                rcVcopy(line.b, &tile->verts[poly.verts[(v + 1) % poly.vertCount] * 3]);
                line.area = poly.getArea();
                line.source_triangle = source;
                lines.push_back(line);
            }
        }
    }
}

} // namespace probe::nav::cook
