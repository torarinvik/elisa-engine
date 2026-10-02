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

## Gaps

- A rebuild re-rasterises the whole staged scene clipped to one tile; nothing tracks which
  tiles changed geometry touches, so the caller names them. Rebuilds are not cached on disk.
- No distance-driven streaming policy: the caller chooses which tiles to load. `NavStream`
  models such a policy but is not wired to these calls.
- No agent or crowd test runs across a tile that is unloaded mid-route.
