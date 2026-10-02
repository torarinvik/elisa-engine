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

## Gaps

- Tiles reload only from their own cook. There is no rebuilding of a single tile from changed
  geometry.
- No distance-driven streaming policy: the caller chooses which tiles to load. `NavStream`
  models such a policy but is not wired to these calls.
- No agent or crowd test runs across a tile that is unloaded mid-route.
