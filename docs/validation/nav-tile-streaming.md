# Navmesh tile streaming (N02)

`NavTiles` (`src/nav/tiles.elisa`) unloads and reloads single tiles of a mesh built by
`NavCook::cook`. A tile is named by its column and row in the cook grid (`NavCook::info`).

- `unload_tile` removes the tile from the Detour mesh. Routes through it stop at its border
  (`Unreachable`). A goal on the removed tile has no polygon (`OffMesh`).
- `load_tile` re-adds the tile from the blob the mesh was cooked from, and Detour re-links it
  to its neighbours.
- `loaded` reports whether the tile is currently in the mesh.
- A stale handle, an unloaded mesh, or a plain `NavMesh::bake` mesh raises `Stale`.
- A tile outside the grid, a tile the cook left empty, an unload of an unloaded tile, or a load
  of a loaded tile raises `NoTile`.

Detour salts polygon references whenever a tile is removed, so references taken before an
unload never resolve to the reloaded tile. The native side is
`native/navigation_stream_abi.inc`. The service keeps each cooked slot's blob, tagged with the
generation it was cooked for.

## Validation

`test/nav_tiles_native.elisa` runs in the gate after `nav_cook_native`. It cooks a 40 m corridor
into 7 × 1 tiles of 6.4 m and checks the following (exit codes 1–15):

- The full route is `Found`.
- With the middle tile unloaded, the route is `Unreachable`, and a second unload is refused.
- After the middle tile is reloaded, the route is `Found` again, and a second load is refused.
- With the goal's tile unloaded, the route is `OffMesh`; after it is reloaded, the route is
  `Found`.
- Tiles outside the grid are refused.
- A baked mesh and an unloaded mesh both raise `Stale`.

As a negative control, expecting `Found` after the cut fails with 6. The test also passes when
the service is built with `-fsanitize=address,undefined`. That run was done by hand; it is not
part of the gate.

## Rebuilding one tile

`NavTiles::rebuild_tile(handle, tx, tz)` rebuilds one tile from the scene staged now, with the
agent, cell and tile settings the mesh was cooked with. The grid stays where the cook put it in
x and z, and only grows upward or downward for new geometry. A loaded tile is swapped in place.
An unloaded tile stays unloaded, and a later load brings back the rebuilt tile, not the cooked
one. The call returns the new tile's polygon count; 0 means the tile is now empty.

`test/nav_tiles_native.elisa` codes 16–20 check this on the corridor:

- A wall staged across the middle tile and rebuilt into it makes the route `Unreachable`.
  The cut survives an unload and reload.
- A rebuild of the cleared floor while the tile is unloaded leaves it unloaded, and the route
  stays `Unreachable` until the tile is loaded; then it is `Found`.
- Rebuilding a tile outside the grid is refused, and a baked or unloaded mesh raises `Stale`.

As a negative control, staging no wall fails with 16. The test passes under
`-fsanitize=address,undefined` (by hand).

## Streaming by distance

`NavTileStream::step(handle, x, z, radius, budget)` (`src/nav/tile_stream.elisa`) reads
the cooked grid through `elisa_navigation_v1_tile_grid` and wants every non-empty tile
whose rectangle lies within `radius` of the focus loaded and every other tile out. It makes
at most `budget` changes per call and reports loaded, unloaded and still-pending counts.
`test/nav_tile_stream_native.elisa` (in the gate) checks, on the 7-tile corridor:

- Focus at x=1 with radius 3 and budget 2 unloads 2, 2, 2 tiles (pending 4, 2, 0); the
  route is `Unreachable` midway and `OffMesh` once the goal tile is out; a fourth step
  changes nothing.
- A radius covering the corridor loads all six back in one step and the route is `Found`.
- Focus at x=39 with radius 4 keeps tiles 5 and 6 and unloads five.
- After the mesh is unloaded, `step` raises `Stale`.

As a negative control, expecting pending 3 after the first step fails with 4. The test
passes under `-fsanitize=address,undefined` (by hand).

## Gaps

- A rebuild re-rasterises the whole staged scene clipped to one tile; nothing tracks which
  tiles changed geometry touches, so the caller names them. Rebuilds are not cached on disk.
- `NavTileStream` visits tiles in grid order, not nearest-first, and handles at most 64
  tiles per mesh. The older abstract `NavStream` model is not wired to it.
- No agent or crowd test runs across a tile that is unloaded mid-route.
