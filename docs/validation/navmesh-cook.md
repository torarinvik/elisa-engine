# Navmesh cook and cache

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is N01.

## Design

- `native/navmesh_cook.h` cooks the staged scene into tiles of 16–512 voxels
  a side (at most 64 tiles, 4096 polygons each). Each tile is a Recast build
  with a border of `walkableRadius + 3` cells, so seams match. The result is a
  versioned `ENAV` blob: a header (source and settings digests, tile grid,
  origin, polygon count and a per-area polygon histogram), one record per
  tile (coordinates, polygon count, data size), the Detour tile data and a
  trailing FNV-1a digest. Digests read explicit fields, never padded structs,
  so the same input and profile give a byte-identical blob.
- `load` validates the magic, version, digest, header and every tile record
  before adding any tile to a multi-tile `dtNavMesh`.
- `native/navmesh_cook_cache.h` keeps up to 8 cooked scenes by id. A lookup
  hits only when both the geometry and the profile/tile settings digest match.
  A changed scene drops its old entry (`Invalidated`). The cache saves to and
  loads from an `NACC` file; a load replaces the cache only when every entry
  validates.
- Diagnostics have a code, a severity and a subject (box, tile, volume or
  link). Errors: invalid input, grid too large, no walkable surface, empty
  mesh, tile capacity, corrupt or old cache. Warnings: an area volume over no
  ground, and a link endpoint that misses the mesh.
- Overlay lines are polygon edges and link segments. Each polygon edge names
  the staged box whose walkable triangle lies under the polygon's centre.
- `src/nav/cook.elisa` (`NavCook`) wraps the ABI.
  `src/nav/cook_plan.elisa` (`NavCookPlan`) holds the tile/grid/cache-reuse
  arithmetic; `proof/nav_cook_plan.elisa` proves it.

## Checks

- `test/navmesh_cook_test.cpp` passes under ASan/UBSan
  (`scripts/run_boundary_sanitized.py`). It cooks a scene with two rooms, a
  doorway wall, a pillar, six stairs to a mezzanine, a door area and a drop
  link. It checks:
  - Two direct cooks are byte-identical (`memcmp`).
  - A flipped byte is refused as corrupt, and a bumped version as an old
    version.
  - The cook spans more than one tile and has door-area polygons.
  - A route climbs from the first room to the mezzanine.
  - The overlay names the mezzanine box and shows the link.
  - Cache outcomes: miss, then hit with the same digest; clear and re-cook
    gives the same digest and size; a new radius invalidates; a moved pillar
    invalidates.
  - The cache saves, clears and loads back, and the loaded entry hits. A
    corrupt file is diagnosed and leaves the cache unchanged.
  - Bad input: an empty scene, a tile size of 4, a cube on its corner (no
    walkable surface), a box too small for the agent (empty mesh), an oversized
    grid, and the two warnings.
- `test/nav_cook_native.elisa` repeats the scenario through `NavCook`.
  `scripts/build_nav_native_test.sh` builds it, and the gate runs it (codes
  1–17, 30–39). Negative controls: inverting the overlay check exits 6, and
  expecting `CACHE_VERSION` for a non-cache file exits 16.
- `test/nav_cook_plan.elisa` checks the planning arithmetic, and
  `proof/nav_cook_plan.elisa` proves it with 0 findings.

## Gaps

- A cache hit does not replay the warnings from the original cook.
- Overlay source mapping uses the polygon centre only. A polygon that
  straddles two boxes is attributed to one of them.
- No game consumer cooks yet; the character course still uses
  `NavMesh::bake`. Runtime tile streaming (N02) is separate.
