# Character course doors, ramps, drop links and area costs

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`), SDL3/Metal, Wicked, Jolt and
Recast/Detour.

This extends [`course-navigation.md`](course-navigation.md) with the N01–N03
pieces it left open: rotated geometry, area annotations and costs, off-mesh
links, raycasts, and an agent that goes through a door and over a platform
link.

## Design

- **Oriented boxes.** `elisa_navigation_v1_add_oriented_box` stages a box
  rotated by a unit quaternion (within 1e-3 of unit length). The course now
  stages every box through `NavMesh::add_oriented_box`, so the tilted ramp
  and the summit are on the navmesh.
- **Area volumes.** `mark_area(low, high, area)` marks an axis-aligned
  volume (up to 16) with a custom area from 3 to 15. The marks are applied
  after erosion (`rcMarkBoxArea`). Walkable polygons are area 1, and link
  polygons are area 2. Every non-null polygon gets flag bit 1, and an area
  below 16 also gets `1 << area`. Disabling an area sets its bit in the
  filter's exclude flags.
- **Per-mesh filters.** `set_area(handle, area, enabled, cost)` changes the
  query filter held by that mesh's slot. The cost must be at least 1. A stale
  handle raises. A rebake resets the slot's filter to all areas open at
  cost 1. `find_route`, `nearest` and `raycast` all use the slot's filter.
- **Off-mesh links.** `add_link(start, end, radius, bidirectional)` stages up
  to 16 links, each with a radius in (0, 8]. Detour marks a link's start in
  the straight path, and `NavRoute::starts_link` exposes that to Elisa. The
  agent follows a link point like any other corner. The Jolt character does
  the drop, so navigation still never writes a transform.
- **Raycast.** `NavMesh::raycast` returns `RayHit{hit, fraction}`. A clear
  ray returns a fraction of 1.
- **Course consumer.** The guide pen's west wall now has a doorway. Its floor
  is marked area 3 and starts disabled. `set_door` creates or destroys the
  door's physics body and render box and enables or disables the area to
  match. The summit has a one-way drop link from (7.5, 2, 2.5) to
  (9, 0, 2.5).

## Checks

`test/navigation_service_test.cpp` codes 30–44 run under ASan and UBSan in
`run_boundary_sanitized.py`. The stage has a floor, a raised platform, an
oriented ramp, a walled pen with a marked doorway, a mud strip (area 4) and
a one-way drop link. The codes cover:

- invalid arguments
- the bake
- a ramp route
- a drop route that uses the link and is shorter than 8
- a reverse route with no link
- the pen reached through the door
- clear and blocked rays
- a closed door giving Unreachable
- a mud cost of 50 that is walked around
- invalid `set_area` calls
- per-mesh filters, and a rebake that starts open
- invalid links and rays
- unload and stale handles
- volume and link capacity

`test/nav_agent.elisa` codes 17 and 18 check that route link marks are set
and cleared.

The course self-test (`character-course-smoke`) adds these codes:

| Code | Failure |
|---|---|
| 120 | the floor-to-summit route is not Found, ends below 1.5 m, or uses a link |
| 121 | the walker character does not climb the ramp to the summit within 900 ticks |
| 122 | the summit-to-floor route is not Found, has no link, or is longer than 8 m |
| 123 | the walker does not drop and arrive within 0.5 m of the goal on the floor |
| 124 | the reverse route uses the one-way link, or is shorter than 8 m |
| 125 | with the door open, the plan into the pen is not Moving with a Found route |
| 126 | the guide does not arrive inside the pen |
| 127 | after the door closes behind it, the replan home is not Unreachable |
| 128 | the guide does not end Stranded inside the pen |
| 129 | the ray across the guide wall is not hit with 0 < fraction < 1 |
| 130 | the ray along open floor is hit, or its fraction is not 1 |

## Results

- The course smoke passes (status 0).
- Negative control 1: without `add_link`, the smoke fails at 122.
- Negative control 2: with the door area left enabled while the door body is
  closed, the smoke fails at 106. The old closed-pen check sees a route into
  the pen.
- Negative control 3: staging only unrotated boxes (so the ramp is missing)
  makes the smoke fail at 120.
- The navigation ABI test passes under ASan and UBSan. The native gate's
  Recast probe still routes over 8 polygons.
- The full check and the native gate pass (all stages 0, including headless and source length).

## Gaps

- Tiled or cached bakes, multi-room/stair cooks, bake diagnostics and debug
  overlays remain open (N01).
- Multi-tile streaming is still open (N02, N06).
- Links are followed as straight corners. There is no jump or climb
  animation, and there is no link-traversal state beyond the corner.
- Nobody has watched the door, the climb or the drop in manual play.
