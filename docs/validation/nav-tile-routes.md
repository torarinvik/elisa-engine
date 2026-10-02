# Navigation: route requests over streamed tiles (N06)

`NavTileRoutes` (`src/nav/tile_routes.elisa`) tags route requests on a cooked, streamed
navmesh with the mesh's tile epoch (`elisa_navigation_v1_tile_epoch`). The native service
bumps the epoch on every successful tile load, unload and rebuild, including the removal
half of a rebuild that later fails. A caller takes a `Ticket` when it issues a request and
calls `deliver(ticket)` before giving the route to an agent; `Moved` means a tile changed
in between and the request must be reissued.

## Validation

`test/nav_tile_routes_native.elisa` (in the gate, through `scripts/build_nav_native_test.sh`)
on the 7-tile corridor:

- A route found before the middle tile is unloaded is refused as `Moved` (code 4). One
  requested after the cut is delivered and is `Unreachable`.
- An agent assigned the cut route strands at its end, before the cut (code 7).
- `NavTileStream::step` brings the tile back; the earlier ticket is refused, a fresh request
  from the stranded position is `Found`, delivered, and the agent reaches `Arrived`
  (codes 8–11).
- Rebuilding a tile, and unloading and reloading one, also refuse older tickets (12–13).
- Each tile also keeps its own epoch. `ticket_area` watches only the tiles an x/z box
  touches: unloading and reloading a far tile leaves it deliverable, rebuilding a tile in
  the box refuses it (15–17).
- After the mesh is unloaded, delivery and new tickets raise `Stale` (14).

Negative controls: removing the epoch bump on unload fails with 4, dropping the per-tile
bump also fails with 4, and summing the mesh-wide epoch for an area fails with 16. The test passes
under `-fsanitize=address,undefined` (by hand).

## Gaps

- An area ticket sees only the tiles in its box; a caller must pass a box that holds the
  whole route (points plus agent radius), or use `ticket` to watch every tile.
- Obstacles are applied as per-tile rebuilds (`NavTileStream::rebuild_area`) by design;
  DetourTileCache is not bound.
- Queries run synchronously; the ticket guards the interval between issuing a request and
  using its result, not a background query thread.
