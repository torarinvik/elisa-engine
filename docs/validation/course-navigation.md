# Character course navigation and animated rigs

Validated on 2026-09-27 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler, SDL3/Metal, Wicked, Jolt and Recast/Detour.

This is delivery-queue item 7: one navigation consumer and two independently
animated instances in the second playable client (`examples/character_course`).

## Design

- **Recast/Detour from Elisa.** `src/nav/navmesh.elisa` is an opt-in public
  module over `native/navigation_service_abi.cpp`. It exposes box staging
  (`begin`/`add_box`), `bake` for an agent `Profile`, generation-tagged
  `Handle`s with `live`, `unload` and `live_count`, `find_route` and
  `nearest`. A goal that cannot be reached, or an endpoint that is off the
  mesh, comes back as a route status. A stale handle raises an error. At most
  64 boxes and 8 live meshes are allowed. `elisa_build_run.py` links the
  service and the Recast/Detour static libraries into every native
  application.
- **Movement policy stays in Elisa.** `src/nav/route.elisa` holds a bounded
  corridor of up to 64 points and its status. `src/nav/agent.elisa` does the
  corridor following, arrival, stranding at a stopped-short route, a
  truncated-route replan request, and stuck detection (both held in place and
  crawling). The agent only returns a direction; the course's Jolt character
  controller moves the body. Navigation never writes a transform.
- **The course consumer.** Every axis-aligned course box is staged as it is
  created. The tilted ramp is left out, so no route passes under or over it.
  The guide yard adds a 3.5 m wall and a closed pen. Both are 1.2 m tall,
  shorter than the 1.4 m agent, so Recast keeps them solid. After the bake, a
  second Jolt character (the guide) patrols between two posts on either side
  of the wall. It turns around when it arrives, and plans again from wherever
  it stands whenever it is not moving (stuck, stranded, or pushed).
- **Animated rigs.** `rigs/guide_rig.pkg` is the engine's synthetic two-joint
  skinned panel with its looping `lift` clip, cooked by `make_rigs.py`. The
  cook is deterministic, and CI checks the committed bytes. The course loads
  it twice and plays the clip at 1.0× and 0.5×. Each frame's step is clamped
  to 0.25 s, so a stall cannot skip cycles. If the package is missing, play
  continues without the rigs.

## Checks

`test/nav_agent.elisa` is in the shared check (codes 1–25). It covers:

- idle
- ordered corners and arrival timing
- stranded and truncated routes
- blocked routes with zero steering
- held and crawling stuck detection
- replan and resume
- the 64-point route bound
- malformed routes (oversized point count and non-finite waypoint) rejected at assignment
- non-finite character poses reported as Stuck with zero steering

`test/navigation_service_test.cpp` runs the service ABI under ASan and UBSan
(`run_boundary_sanitized.py`). It covers:

- the version check
- staging capacity
- bake and publication
- a route around an obstacle
- an unreachable goal, off-mesh endpoints, and nearest
- a stale handle after unload
- a rebake with a new generation
- slot capacity
- invalid arguments

The course self-test (`character-course-smoke`) runs the real bake, character
controller and renderer:

| Code | Failure |
|---|---|
| 100 | the baked mesh is not live, or `live_count` ≠ 1 |
| 101 | the outbound plan is not Moving with a Found route |
| 102 | the route does not pass the wall's end |
| 103 | the guide does not arrive at post B within 900 ticks (15 s) |
| 104 | the guide crossed the wall, or stopped more than 0.5 m from post B |
| 105 | the distance walked is outside [length − 1, 1.5 × length + 1] m |
| 106 | a goal inside the pen is not Moving with an Unreachable route |
| 107 | walking toward the pen does not end Stranded within 1800 ticks |
| 108 | the guide ended inside the pen |
| 109 | a goal 40 m above the course is not Blocked |
| 110 | the nearest point to a raised probe does not snap to the floor |
| 111 | after unload, the mesh is still live, `live_count` ≠ 0, or the handle is not stale |
| 112 | the rebake reused the generation, is not live, or revived the old handle |
| 113 | the route home on the rebaked mesh is not Found |
| 114 | the rig package did not load twice |
| 115 | after two 0.2 s advances, progress is not 0.4 / 0.2 |
| 116 | seeking the first rig to 0.9 changed the second |
| 117 | the second rig does not advance to 0.3 after the first is destroyed |
| 118 | the destroyed rig's handle does not report `UnknownHandle` |
| 119 | a navmesh is still live after the self-test unloads the guide's mesh |

## Results

- The course smoke passes (status 0) with every check above.
- Negative control 1: I dropped the guide wall from the navmesh but kept its
  physics body and render box. The smoke failed at 102, because the route
  went straight through the wall.
- Negative control 2: with the same change and check 102 inverted, the smoke
  failed at 103. Jolt held the guide at the wall, so it never arrived. The
  consumer really depends on the baked obstacle.
- `nav_agent` passes in the shared check. The navigation ABI test passes under
  ASan and UBSan in the gate's headless stage. Leak detection is off, as it is
  for the other harnesses: ASan does not support it on macOS arm64.
- On 2026-10-06, the Stage1 `nav_agent` test passes codes 21–25 for malformed
  routes and non-finite poses; malformed routes become Blocked before corridor
  indexing, and invalid poses request a replan instead of looking Arrived.
- `navmesh_service.h` now reports a Detour route that stops short of the
  goal polygon as Partial instead of Success. This exposed a flaw in the
  native gate's Recast probe (`native/recast_probe.h`): its one-cell gap had
  always eroded shut under the agent radius. Its "path around the wall" was a
  partial route that stopped at z = 9.2 on the near side. The probe now has a
  three-cell gap and requires `reaches_goal`. It routes to (18, 18) over 8
  polygons and passes under ASan and UBSan.
- `make_rigs.py --check` reproduces `rigs/guide_rig.pkg` (9,899 bytes)
  exactly.

## Gaps

- Update 2026-09-28: the ramp, door areas, area costs and drop links are now
  staged and checked. See
  [`course-navigation-links.md`](course-navigation-links.md). Tiled or cached
  bakes and debug overlays remain open (N01).
- The keyed course rig now uses the shared production Ozz animation library;
  legacy packages retain the fixed-rate fallback. The native render smoke
  checks independent playback and allocation-free steady ticks, and the
  optimized clone benchmark records keyed CPU p50/p95/p99. The two posted
  course rigs use separate `create_mesh` instances; animated clone ownership
  and shared source/library state are covered by group 236 and the benchmark.
- The rigs' rendered motion is checked through playback progress and the
  existing SDL3/Metal skinning smokes. Nobody has yet watched the guide patrol
  or the rigs in manual play.
