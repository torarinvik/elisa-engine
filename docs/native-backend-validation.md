# Native backend validation

The engine's SDL3 boundary and Godot host contract are checked by the regular
ElisaScript workflow. The full Wicked renderer remains a separate native spike;
its source is kept outside this repository so upstream code and build artifacts
do not become Elisa source.

## Godot

Homebrew Godot 4.7.2 was installed on 2026-09-17. The headless probe runs with:

```sh
godot --headless --path backends/godot --script backends/godot/probe.gd
```

It applies the portable create/update/destroy lifecycle to a real Godot
`MeshInstance3D`, `StandardMaterial3D`, and `Camera3D`, then validates epoch/entity
identity, transform update, and destruction ordering. It does not claim a
GDExtension or pixel-level renderer equivalence.

## Wicked

The upstream checkout was cloned beside this repository at revision
`5e07e3bfd7f89633a468009e0620b14e508dd8b7` (2026-09-17). It is not copied into
the engine repository. The arm64 Debug/O0 CMake build produces the static
Wicked, Jolt, Utility, FAudio, and Lua libraries needed by the probe. The
release build still hits an Apple Clang frontend crash in `wiPrimitive.cpp`,
and the optional upstream `offlineshadercompiler` target needs additional
Apple framework link flags.

The repository contains a small native scene probe in
`native/wicked_probe.cpp`, driven by `scripts/wicked_probe.elisascript`. It
creates an Elisa-named cube and camera, moves the cube, configures a Metal
swapchain through Wicked's `Application` and `RenderPath3D`, renders one hidden
frame, verifies the render target, and removes every entity. The probe uses a
one-shot process because this Wicked revision has no public shutdown API for
its global worker systems; it still checks Elisa-side despawn before exit. This
is a real Metal frame smoke test, not yet a complete GDExtension or game loop.

Executed 2026-09-18 on this workstation with defaults
(`WICKED_ROOT`/`WICKED_BUILD` unset, resolving to the sibling checkout and
`build-elisa-arm-o0`): wicked 0.72.114, `GraphicsDevice_Metal` created,
Jolt 5.6.0 / Lua 5.4.8 / FAudio 25.1.0 initialized, `scene
create/update/render passed`, `scene despawn passed`, exit 0. This is the
first in-session executed evidence for the native path; earlier records
described the setup without a fresh run.

## Frame capture (in progress, currently red)

`native/wicked_probe.cpp` now saves its frame to
`build/wicked-frame.png` and `scripts/wicked_probe.elisascript` checks it
with `scripts/compare_renders.py` (stdlib-only PNG parse, dimension scale
check, blank detection, peak-plus-mean tolerance compare mirroring
`src/backend/image_compare.elisa`). The plumbing is verified end to end;
the pixels are not there yet, and the driver fails loudly on that fact.

Ground truths established by probe diagnostics so far (all printed by the
probe on every run):

- Argless `Run()` on a hidden window draws nothing: the window is never
  active, so the driver passes an `alwaysactive` flag through
  `wi::arguments::Parse`, and runs five frames plus `WaitForGPU`.
- `CameraComponent::At` is a facing *direction*, not a target point; the
  default orientation already faces +Z toward the cube, and a 180-degree
  flip was tried and reverted after the frustum dump proved it wrong.
- `TransformCamera` refreshes view matrices only; without an explicit
  `UpdateCamera` the frustum stays stale and culls everything. With it,
  visibility reports 1 object and 1 light, and the cube AABB is exactly
  min=(-1,-1,0) max=(1,1,2).
- `CreateScreenshotWithAlphaBackground` re-renders the *postprocess*
  result, which sits stale when no post effects run; the probe captures
  the swapchain backbuffer via `saveTextureToFile` instead.
- The captured frame is 640x400 (Retina 2x of the 320x200 manifest
  viewport), so the dimension check accepts integer scales.

Ruled out by invariant pixels across runs: lamp existence and intensity
(4 to 30), lamp transform commit, camera orientation both ways, one
versus thirty frames, event pumping, GPU drain, white versus sky-blue
emissive material, MSAA resolve (aliased at 1x), depth target validity
(640x400 present), mesh upload (24 verts, 36 indices, buffers valid),
internal resolution (640x400, so the scissor path is not degenerate),
object render flags and mesh-index linkage (all drawable), close-range
framing (a frame-filling cube changes nothing), and a realistic-sky
control (still black, so the failure is in the shared path, not scene
setup). A Metal System Trace of the probe shows exactly one
probe-owned shader (a compute entry) and no runtime shader compiles.
That alone proves little either way: the checkout ships hundreds of
prebuilt object permutations, so draws could be served silently from
binaries. What constrains the failure instead is elimination across
independent controls: a frame-filling close-range cube, a realistic-sky
background (which needs no objects, lights, or culling), and white
versus emissive materials all produce byte-identical near-black, while
CPU state is verified correct at every level (camera 640x400 physical
with sane near/far/fov, cube world matrix exact, AABB exact,
visibility 1+1 with correct index linkage, valid depth/MSAA targets,
uploaded buffers, sane object flags, occlusion culling disabled without
effect). A one-run triage saved the scene target, its MSAA resolve
alias, and the presented image side by side: all three read pure zero,
so no color stage holds the scene and compositing is not the culprit.
Forcing the depth clear to 1.0 changed nothing versus the 0.0 default,
so depth convention is not the sole gate either (with the caveat that a
per-frame target recreate would wipe the forcing; the desc print showed
stable 640x400 throughout). A setup defect would move at
least one of those controls; none moves. So the draw is either never
submitted or fully discarded downstream of submission: look next at the
per-instance submit path (render queue, PSO and material bind),
viewport binding values, tonemap exposure input, light-grid upload, and
the HDR compositing branch. The
single most informative next step is not another engine-Code read but
an environment control: a minimal raw-Metal triangle through the same
hidden SDL window. If the triangle shows, the environment rasterizes
and the failure is Wicked draw submission; if it does not, the hidden
surface itself never presents pixels regardless of engine code. Two
methodological notes: `saveTextureToMemory`
must never be pointed at a depth target (Depth32Float_Stencil8 to
buffer trips validation outright), and the probe binary must receive
the inner `WickedEngine/WickedEngine` directory — the outer checkout
root silently starves every shader lookup and segfaults. That is the
next debugging step, not a new policy: the capture, compare, and gate
plumbing is done and waiting for first light.

The manifest the probe consumes (`backends/scene_manifest.txt`) is pinned
to the Elisa canonical scene by `scripts/record_validation.py`, which
rejects drift in version, epoch, entity, camera, viewport, and command
sequence. Positions remain scenario data and are intentionally unpinned.

When the external checkout has been built as `build-elisa-arm-o0`, run:

```sh
WICKED_ROOT="../WickedEngine" \
WICKED_BUILD="../WickedEngine/build-elisa-arm-o0" \
elisascript scripts/wicked_probe.elisascript
```

Set `WICKED_SDL_INCLUDE_DIR` and `WICKED_SDL_LIB_DIR` when SDL2 is installed
outside Homebrew's `/opt/homebrew` prefix.

The current macOS build needs four local compatibility edits in that external
checkout: a `PipelineHash` inequality operator, the SDL2 Apple cursor guard,
and 8-byte alignment attributes for the two FAudio default curves. The probe
also passes `WI_UNORDERED_MAP_TYPE=2` and `WICKED_CMAKE_BUILD` so its ABI agrees
with the CMake libraries. Those edits and flags are deliberately kept outside
this repository; the command above is the reproducible acceptance gate for the
native scene path.

## First light (2026-09-18)

The black frame is resolved: the `build-elisa-arm-o0` tree was linking
stale `wiRenderer.cpp` objects that predated the checked-out sources
(the `WickedEngine_ext_shaders` target, not `WickedEngine_common`,
owns that translation unit — asking cmake for the wrong target name
reports success while building nothing). Temporary in-upstream prints
(not committed, reverted afterward with the checkout verified back to
its four documented compat edits) showed the per-batch path reached
with a missing pipeline; after a genuine rebuild from pristine
sources the same probe renders a centered bright rectangle
(x 223-416, y 103-296 of the 640x400 frame) and exits 0 through the
tolerance check. Two runs — one against the instrumented library, one
pristine — produce byte-identical PNGs (peak and mean error both
0.0000), so the renderer is deterministic for this scene and the fix
is permanent, not timing luck. Lessons: always verify a rebuild by
object timestamp rather than a bare exit code, and distrust implied
target names. The upstream checkout itself is untouched by the fix;
only its build products were stale.

## Maze geometry and determinism (2026-09-18)

The native host now renders game-authored content, not a test cube.
`examples/maze/layout.elisa` derives the 34 wall cells from
`Maze::is_wall`; `test/maze.elisa` checks that list against the
topology in both directions (every listed cell is a wall, every wall is
listed); `backends/scene_manifest.txt` carries the list to the probe as
a `walls=` line whose count `scripts/record_validation.py` re-checks
against the same 34 the Elisa test pins; and `native/wicked_probe.cpp`
builds one unlit cube per cell on a vertical plane the default frustum
contains. The driver runs the probe twice, requires a strict
zero-tolerance pixel compare, and then verifies the frame cell by cell:
every projected grid position must be bright exactly when it is a wall
or a declared game marker, and dark otherwise. The earlier
structure-only sampling and its interior drift are gone; the check is
now exact and the markers are colour-checked separately.

## Godot rendered capture (2026-09-18)

Godot is no longer command-lifecycle only. `backends/godot/capture.gd`
consumes the same `backends/scene_manifest.txt`, builds the same 8x8
wall map and entity cube, and reads the root viewport back to PNG.
`--headless` cannot do this — it forces Godot's dummy rendering driver —
so `scripts/godot_capture.elisascript` runs Godot with the real display
driver, which works here (Metal-backed OpenGL compatibility device) and
needs a window server, unlike the main gate's headless probe.

The runner gates the same claims as the native driver and all pass:
non-blank (320x200), byte-identical across two captures
(`peak=0.0000 mean=0.0000`), exact topology agreement cell by cell, each
game marker in its own colour at its own cell, and identical topology
grids between the Godot and Wicked frames. That last check is the plan's
"scene semantics agree across backends" made executable; it compares
structure and per-marker colour, not per-pixel shading, since the two
renderers tonemap differently.

## Handedness correction (2026-09-18)

Building that comparison surfaced a real convention defect: the native
frame was the mirror of the Godot frame. Elisa's math is right-handed
with -Z forward (`src/math/geometry.elisa`) and Godot matches it, but
Wicked is left-handed, so its screen-right is Elisa's +X. The native
bridge now negates X when handing positions to Wicked (`to_wicked_space`
in `native/wicked_probe.cpp`), which is the RH-to-LH conversion at the
boundary the plan calls for. Evidence: before the fix the two topology
grids disagreed on interior walls; after it they are identical and each
matches the Elisa wall list exactly on the maze-only fixture.

## Gameplay drives the rendered scene (2026-09-18)

Both hosts render the Elisa game, not just geometry.
`examples/maze/trace.elisa` plays scripted input through the real
`MazeGame`, so collision, the key, the locked door, hazards, and the goal
are resolved by engine rules. It publishes two runs: the ten-move win
path, pinned by `test/maze.elisa` as won, and a three-move mid-game
snapshot used by the fixture so every marker is still ahead of the player
and therefore visible. The column-1 route avoids the hazard at (2,5) that
resets a naive path.

`examples/maze/game.elisa` now publishes the marker coordinates
(`maze_key_x/y`, `maze_door_x/y`, `maze_hazard_a/b_x/y`); the test checks
each against the predicate that owns it (`is_key`, `is_door`,
`is_hazard`) and that none is a wall. `backends/scene_manifest.txt`
carries `player_final`, `goal`, `key`, `door`, and `hazards`, and
`scripts/record_validation.py` rejects drift in any of them, an
out-of-grid cell, a marker on a wall, overlapping markers, or a player
standing on a marker.

The native and Godot hosts each draw the player cell-sized on the marker
plane plus one unlit cube per game object, and `compare_renders.py verify`
now gates four claims per host: dimensions, non-blank, run-to-run
determinism, topology agreement, and finally that each game object shows
its own dominant channel at its own cell (goal green, key yellow, door
magenta, hazards red, player blue) so a right-shape-wrong-place or
wrong-colour host fails. Both hosts pass. A note for future readers: both
probes emit RGBA PNGs even though the native swapchain is BGRA, so the
checker reads red first.

## Navigating character (2026-09-18)

`examples/maze/hunter.elisa` adds the gameplay half of Phase 5's
"moves, collides, navigates": a character that asks the navigation
module for a fresh shortest path over the maze walls each step and takes
one cell at a time. It can never cross a wall, it reports a bounded
deterministic run (start at (1,1), reach (2,6) in six steps), and a
walled target yields no route so it does not move at all. The spawn cell
is published (`hunter_spawn_x/y`) and the test pins it to a valid open
cell; `backends/scene_manifest.txt` carries `hunter=1,1`, the validator
rejects drift or a spawn on the player/another marker, and both hosts
draw the character with its own colour, which the checker verifies.
What remains for this character is skeletal animation and unloading,
which need the ozz integration; navigation itself is done and gated.

## Game status as portable data (2026-09-18)

The hosts also consume the game's state, not just its geometry.
`examples/maze/game.elisa` publishes `game_status_code`, mapping the
`GameStatus` enum to a small integer so a host never needs the engine's
enum spelling; `examples/maze/trace.elisa` publishes the mid-game
snapshot's status, and `test/maze.elisa` pins the mapping (Playing is 1,
Won 3, Lost 4, and so on). The fixture carries `game_status=playing` and
a `status_cell`; the validator rejects an unknown status name or a
status cell that is out of grid, a wall, or overlapping another marker.

Both hosts colour a cell-sized indicator by status (playing cyan, paused
yellow, won green, lost red, otherwise neutral), and `compare_renders.py`
verifies the fixture's status colour at that cell, so a host that ignored
the status field would show the neutral default and fail. This is the
menu/state half of Phase 4 made executable without a UI toolkit. Audio cues
are consumed by the native host (decoded clip plus one play per cue) and now by
the Godot host as well; packaging for the validated target is recorded beneath
("Reproducible release packaging").

## Measured frame time (2026-09-18)

Both hosts now report measured frame time instead of a "zero overhead"
claim, and each runner gates it against the fixture's
`frame_budget_us=16667` (the plan's 60 Hz target) using
`compare_renders.py perf`, which rejects a median or p95 over budget.
Measurements on this workstation: the native host medians around 1.3 ms
with p95 near 4.2 ms; Godot medians around 0.4 ms with p95 near 0.9 ms.
Method matters and is stated so the numbers are not overread: five
warm-up frames are excluded because shader-permutation creation makes the
first frames unrepresentative; the native host times `application.Run()`
with a steady clock; the Godot host disables vsync before timing, because
otherwise `await process_frame` measures the display frame boundary
rather than the host's own work (Godot's `TIME_PROCESS` monitor rounds to
zero for a scene this small and is not used). These are trivial, mostly
hidden frames, so the figures are a floor for this scene, not a
performance promise for a full game.

The native host also records how frame time scales with the live entity
count: it adds off-camera cubes at 16, 128, and 512 entities, measures
twelve frames at each size, prints the median and tail, removes them
again, and **fails the run if the largest median exceeds the fixture's
frame budget**. Observed medians around 1.7 ms, 2.3 ms, and 3.1 ms
respectively — a clear but sub-linear rise, all inside the 16.7 ms
budget. The sweep lives in `native/perf_sweep.h`. This is measured
scaling for one small scene, not a claim about a full game's workload.

## Offline asset cooking (2026-09-18)

The pipeline's cooking stage now runs offline, as the plan intends
("keep complex importers out of the shipped runtime"). The validation
workflow invokes `scripts/cook_assets.py`, which parses the fixture's
`mesh_asset`, refuses to produce a package whose normalized counts
disagree with the fixture, and writes `build/cooked/maze_tile.pkg` — a
versioned package (`format=elisa-cooked-v1`) carrying the source path, a
SHA-256 of the source bytes, triangle and position counts, and bounds.
`record_validation.py` refuses to write a validation record if cooking
fails and stores the package name, hash, and size in
`build/validation.json`. Verified both ways: changing the fixture's
`mesh_triangles` to 99 makes cooking exit 1 with
`cooked triangles 12 disagree with the fixture's 99`, and restoring it
cooks cleanly. The package has since grown to `elisa-cooked-v2`, which carries geometry:
float32 positions and normals plus uint32 indices, so a host can build a
mesh from normalized data alone. Both hosts do exactly that — the native
host rebuilds its mesh component and re-uploads, and the Godot host builds
an `ArrayMesh` — and both use it for the goal marker, so the authored
asset is *visible* and verified by the existing goal colour check rather
than by a separate printout. Observed: native `cooked package: loaded=1
format=elisa-cooked-v2 triangles=12 positions=24`, Godot `mesh vertices=24
indices=36`, with topology, marker, fog, and route checks all still exact.
The runtime therefore reads normalized, versioned geometry rather than a
source format. What remains is Elisa-side emission and cooking, which need
the file IO capability `elisac` lacks, and a package that carries a full
scene rather than one mesh.

## Authored asset loading (2026-09-18)

The hosts no longer rely only on geometry built in bridge code.
`examples/maze/assets/maze_tile.gltf` is an authored source asset (a cube
written as glTF with positions, normals, and indices), the fixture names
it with `mesh_asset` and pins `mesh_triangles=12`, and the validator
checks the file exists and the count is positive. The Godot host loads it
through `GLTFDocument` and verifies the imported triangle count —
triangles rather than vertices, because importers split vertices by
normal and UV, so a vertex count is not a stable authoring property.
Observed: `mesh asset: triangles=12 expected=12`.

The native host loads the same file through cgltf, the importer the plan
names. Wicked's built library exposes no model importer, so the dependency
is fetched and hash-pinned by `scripts/fetch_dependencies.py` into
`dependencies/` (git-ignored), which keeps third-party code out of the
Elisa-owned tree; `scripts/wicked_probe.elisascript` refuses to build
without it and says how to fetch it. Observed: `mesh asset:
triangles=12 expected=12 positions=24`, matching the Godot host's count.
This is the pipeline's import stage — source bytes to validated
normalized counts. Creating renderer meshes from that data and cooking a
runtime package remain open, as does Elisa-side emission of a cooked
manifest, which needs a file IO capability `elisac` code does not have.

## Fog of war in the hosts (2026-09-18)## Fog of war in the hosts (2026-09-18)

Both hosts hide level geometry outside the player's visible radius, using
the rule the game owns. `examples/maze/game.elisa` publishes
`maze_fog_radius` and a pure `maze_cell_visible` predicate, and
`test/maze.elisa` pins the radius (3) and the predicate at the boundary.
The fixture carries `fog_radius`, and the checker expects brightness only
for walls inside the radius plus the markers, so a host that drew every
wall would fail on the 25 hidden cells. Observed in both hosts:
`walls=9 hidden=25`, with topology, marker colour, and route checks all
still exact. This is the plan's "visibility or fog-of-war" rendered from
Elisa-owned rules rather than re-implemented in a backend.

## Environment limits found (2026-09-18)

Plain Elisa programs have no file or socket I/O: the host APIs
(`write_text`, `path`, process capture) belong to ElisaScript, not to
code compiled by `elisac`. That blocks two plan items at the data layer
rather than the design layer — the asset pipeline cannot yet emit a
cooked manifest to disk, and a live transport cannot be driven from
Elisa across processes. The contracts and encodings for both exist and
are tested (`src/assets/*`, `src/net/wire.elisa`, `src/net/loopback.elisa`);
what is missing is an IO capability to carry them across a real boundary.
Redoing either in C++ would duplicate engine logic and is deliberately
not done.

## Gameplay over time in the hosts (2026-09-18)

The hosts no longer draw a static arrangement; they replay an Elisa
route. `examples/maze/hunter.elisa` publishes the path from the
character's spawn to a fixed pursuit cell in the open first row
(`hunter_pursuit_route`), chosen so it crosses no marker cell, and
`test/maze.elisa` pins its four cells. The fixture carries
`hunter_route`, and both hosts walk it one cell per frame, so the
captured frame shows the character at the route end and its spawn empty.
`compare_renders.py` now checks exactly that: the character's colour at
the route end, and that the spawn cell is dark. A host that ignored the
route would leave the spawn lit and fail, so "movement" is gated, not
merely asserted.

One Wicked detail cost real debugging time and is worth recording:
writing `translation_local` directly does not mark the transform dirty,
so `UpdateTransform()` keeps the stale world matrix and the object never
moves. The fix is to call `SetDirty()` before `UpdateTransform()` when
mutating a transform outside the scene's own systems.

## Native physics integration (2026-09-18)

The native host now drives a real Jolt simulation. The probe enables
simulation, gives a cube a dynamic `RigidBodyPhysicsComponent` (box
shape, unit mass), places it far off-camera at x=20 so it cannot occlude
any projected marker or wall sample, and then settles with a loop that
supplies wall time rather than only frames — Wicked advances Jolt from
the frame delta, so gravity needs real elapsed time. Observed:
`start_y=5.000 end_y=-41.665`, a clear fall, and the run fails if the box
does not drop at least 0.2 units. The game-side policy stays in Elisa
(`src/physics/policy.elisa`: one solver per body, kinematic motion from
Elisa, dynamic motion from the solver result, commits at a tick
boundary); this proves the native solver actually moves a dynamic body.

The Godot host matches it with one `RigidBody3D` off-camera (observed
`start_y=5.000 end_y=2.130`), settled with real timers because Godot also
advances physics from real delta. Each host therefore runs exactly one
solver for a body — Jolt through Wicked on the native side, Godot physics
on the other — never both for the same body, and the image checks stay
exact because the body is outside the frustum.

## Native audio decode and cue playback (2026-09-18)

The native host now exercises the audio path the game's cues need, without
any asset file. It builds a 16-bit mono 8 kHz WAV in memory, decodes it
through `wi::audio::CreateSound`, and asserts the decoded sample count,
rate, and channel count — evidence that real audio bytes were processed,
not merely that a call returned. It then creates and plays one sound
instance per cue the Elisa game emitted for the fixture
(`audio_cues=4`, pinned by `test/maze.elisa` via
`maze_fixture_cue_count` and re-checked by the validator). Observed:
`decoded samples=400 rate=8000 channels=1 cue plays=4`.

The Godot host does not play cues yet; only the native side has audio
integration, and playback is verified structurally (decode plus one play
call per cue), not by listening.

The probe reached 592 of the 600-line file limit, so its support code
(fixture parsing, the right-handed to Wicked-space conversion, cell
markers, and the generated test WAV) moved to `native/probe_support.h`.
The probe has since been split three ways: `native/probe_support.h`
(fixture parsing, handedness conversion, markers, test WAV),
`native/audio_probe.h` (the decode and cue playback check), and
`native/probe_diagnostics.h` (the renderer ground-truth printout).
`native/wicked_probe.cpp` is now 462 lines and each header well under
the limit, verified by rerunning the driver unchanged. Splitting early
is much cheaper than splitting at the limit, which is why it is done as
soon as the count passes roughly 450. `scripts/check.elisascript`
remains unsplit at its own limit.

## Toward pixel comparison (status: both hosts captured)

`src/backend/image_compare.elisa` defines the tolerance policy
(per-channel peak plus mean bound). `scripts/compare_renders.py`
gates both hosts: it parses PNG with the standard library, checks
dimensions, detects blank frames, compares pixels with tolerances, and
samples the projected maze grid for cross-host structural agreement.
The remaining gap is raw colour agreement: the two renderers use
different tonemapping and colour spaces, so a per-pixel colour compare
is not yet meaningful and is not gated. The structural comparison is
the current cross-backend evidence.

## Backend resource accounting (2026-09-18)

`Backend::recorder_live_count` reports how many recorded backend objects are
still live, so a restart or unload is checked against a baseline rather than
trusted. It was added because the two-level `recorder_is_live` wrapper is
declined by the backend when it is called from inside a captured loop body
("could not produce a linkable unit; declined 1: ... (call expression)"),
while a single-level query over the same `seen`/`seen_live` tables lowers
cleanly. The audit now uses the count.

`test/headless_game.elisa` runs eight spawn/despawn/destroy cycles and
requires each to end with the world at zero live entities and the recorder at
zero live objects: the despawned reference is rejected by lookup, storage
slots are reused (`registry_count`/`actor_count` return to zero after
compaction), identities strictly increase (a recycled slot never resurrects
an old identity), and a second destroy of the same reference is refused. The
packaged maze session (`examples/maze/main.elisa`) plays the win route, then
the losing route to the last life, restarts, pauses, resumes, and exits.

## Engine pose evaluation (2026-09-18)

`src/math/geometry.elisa` gained quaternion multiply/rotate/normalize and
transform composition plus a column-major matrix export, so rotation is
engine-owned rather than a vendor type. `src/animation/pose.elisa` evaluates
local-to-model joint transforms, blends two poses by normalized-linear
interpolation, extracts root motion as a position delta, emits a per-joint
skinning payload, and corrects a three-joint chain with `Ik::two_bone_knee`.

The knee solver works in the projection/height plane, so it needs no inverse
trigonometry: a unit chain reaching one unit gives projection 0.5 and height
`sqrt(1 - 0.25)`, and unreachable or collapsed targets straighten or fold the
limb. Tests pin the 90-degree quaternion turn, composition, the identity and
rotated matrix payloads, the pose blend, root motion, and the IK correction in
the gated geometry and animation suites.

## Character composition and unload (2026-09-18)

`examples/maze/character.elisa` is the Phase 5 composition the plan asks for:
each capability is used by a playable thing rather than declared alone. The
enemy owns a World identity and liveness, navigates with the hunter's route,
animates through `Anim`, and carries a three-joint leg pose whose foot is
placed by `Pose::pose_correct_two_bone`; the pose is re-derived from the new
cell on every step. Unloading despawns the entity, so a stale reference is
rejected and the world returns to zero live entities. `test/maze.elisa` pins
the spawn, the hip/knee heights, the move, and the unload.

One backend rule cost time here and is recorded for the next author: a
*qualified* call to an erroring function inside a module (`World::world_spawn`)
is declined with "could not produce a linkable unit; declined N: ... (control
expression)". The same call unqualified after a `using World` lowers cleanly.
Qualified type names are fine; qualified erroring calls are not.

## Reproducible release packaging (2026-09-18)

`scripts/package_release.py` builds the packaged headless game, collects the
cooked asset package and the canonical fixture, writes a `release.json` pinning
the engine commit, dirty flag, platform, and per-file hashes, then produces a
byte-deterministic `.tar.gz` (sorted entries, zeroed mtime/uid/gid, gzip
`mtime=0`). It archives the collected bytes twice and fails unless the two
archives are identical. `scripts/record_validation.py` runs it as part of the
gate and records the archive hash under `release` in `build/validation.json`,
adding `release_packaging` to the checked list.

This is archive reproducibility given the collected inputs, which is the honest
scope: the per-file hashes pin the inputs, while byte-identical compiler output
from source is not claimed. The summary names the supported platform
(`macos-arm64` here) and lists the platforms deliberately left untested
(windows, linux, android, ios), so support is not inferred from a dependency's
platform list.

## Native spawn/despawn churn (2026-09-18)

`native/churn_probe.h` adds the plan's spawn/despawn churn benchmark to the
native host. Eight rounds each create 64 cubes and remove them; every round
asserts the scene's object, mesh, and transform counts return to the pre-batch
baseline, so a leak fails the probe rather than being reported. On this
workstation the round medians were around 2.5-3.4 ms with p95 near 2.8-4.5 ms,
recorded from a `baseline_objects=18` scene. `scripts/wicked_probe.elisascript`
ran end to end: frame verified, frame time within budget, churn passed.

Method note: this is wall-clock create/remove against the live scene, not a
frame time, and the batch is deliberately small so the measurement stays a
churn cost rather than a render cost. It is one recorded workload, not a
performance promise.

## Godot teardown audit (2026-09-18)

`backends/godot/capture.gd` now frees every Elisa geometry node and the
physics body after capturing, waits two frames, and requires the scene root to
return to just its environment node: a missed free leaves a child behind, and a
freed node must report invalid rather than dangling. A run on this workstation
reported `freed=19 remaining=1 environment_alive=true dangling=false`, so the
Phase 4 teardown guarantee ("despawn, restart, and unload must not produce
accumulating resources or dangling references") is now checked on both hosts,
not just natively.

## Godot goal mesh and audio parity (2026-09-18)

Two Godot-host defects/gaps were closed here:

- The Godot host declared `cooked_goal_mesh` before the marker loop but loaded
  the cooked package *after* it, so `use_cooked` was always false and the goal
  marker was the procedural box. This contradicted the README's "both hosts
  build the goal marker's mesh from that package". The package load now runs
  before the marker loop, and a self-check fails the capture unless the goal
  marker actually used the cooked mesh. A run reported
  `goal marker: cooked_mesh=true scale=(0.13, 0.13, 0.13)`.
- The Godot host had no audio at all. It now builds the same short clip the
  native host decodes, verifies the decoded sample information, and queues one
  player per cue the fixture declares, failing if the count disagrees. A run
  reported `godot audio: decoded_bytes=800 rate=8000 cues=4 expected=4`, so
  simple audio now runs on both hosts, not just natively.


## Gated Godot asset and audio checks (2026-09-18)

The cooked-package and audio checks were only in the non-headless capture, so
a regression there would not fail the main gate. They are now in
`backends/godot/probe.gd`, which `scripts/check.elisascript` runs headless:
the probe loads the cooked package, checks the format and triangle count
against the fixture, builds a surface from the geometry, and verifies the
audio clip decodes to the expected sample information. The gate reports
`godot probe asset: format=elisa-cooked-v2 triangles=12 surface=1` and
`godot probe audio: decoded_bytes=800 rate=8000 cues=4`, so the asset and
audio contracts are gated, not only observed.

## meshoptimizer integration (2026-09-18)

The native host now cache-optimizes the cooked package's index buffer with
meshoptimizer before upload (`native/meshopt_probe.h`). Four files are fetched
with revision and content hashes pinned by `scripts/fetch_dependencies.py`, and
the build compiles them into the probe. The measured evidence is the average
cache miss ratio printed before and after the optimization, and the probe fails
if the optimizer makes it worse. For the authored cube the buffer is
`indices=36 vertices=24 acmr=2.0000->2.0000`: the cube duplicates vertices per
face, so there is no reuse for the optimizer to find and the honest result is
no change. The gate therefore prevents a regression rather than claiming a win
this asset does not support. A denser mesh with real reuse would show a
decrease; the instrumentation is what matters here.

## ozz-animation sampling (2026-09-18)

ozz-animation is the plan's selected skeletal-animation library, so the native
host now links it rather than only implementing an engine-side sampler.
`scripts/fetch_ozz.py` pins ozz 0.16.0 by commit
(`6cbdc790123aa4731d82e255df187b3a8a808256`), clones it into `dependencies/ozz`
(git-ignored), and builds only the runtime and offline animation libraries with
CMake — tools, samples, and howtos stay off. `native/ozz_probe.h` builds a
two-joint skeleton and a linear translation clip in code, samples it at ratios
0, 0.5, and 1.0, runs ozz's local-to-model job, and checks the root translation
against the same linear expectation the engine's own sampler produces. The
probe run reported `ozz: root_x start=0.0000 mid=1.0000 end=2.0000`, so ozz is
genuinely linked and sampling. This is not a claim that a full character
pipeline was ported to ozz; the engine-side sampler remains the one the
gameplay uses.

## Recast/Detour navigation (2026-09-18)

The native host now exercises Recast for navmesh generation and Detour for
queries. `scripts/fetch_recast.py` pins recastnavigation v1.6.0 by commit
(`6dc1667f580357e8a2154c28b7867bea7e8ad3a7`), builds only Recast and Detour
(the demo, tests, and examples stay off), and sets
`CMAKE_POLICY_VERSION_MINIMUM=3.5` because the upstream CMakeLists predates the
policy floor of newer CMake. `native/recast_probe.h` rasterizes a plane with a
wall and a gap (counter-clockwise winding so Recast reads +Y normals, and padded
bounds so the ground has vertical headroom), builds the compact heightfield,
regions, contours, poly mesh, and detail mesh, then creates and queries a Detour
navmesh. The path must use more than one polygon, so a straight line being
blocked is what proves Detour navigated. The run reported
`recast: polys=16 navdata=2476 path_polys=5 start=20 end=29`. As with ozz, this
is linked-and-exercised evidence; the maze's own pathfinding stays in Elisa.

## miniaudio integration (2026-09-18)

The native host now exercises miniaudio, the plan's selected audio library.
`scripts/fetch_dependencies.py` pins miniaudio 0.11.22 as a single header, and
`native/miniaudio_probe.h` compiles its implementation into the native probe's
translation unit. The probe decodes the same short WAV the Wicked audio check
uses and opens a null playback device, so audio runs in a headless capture
without an output device; it reports frames, sample rate, channels, and the
active backend, and fails unless the decode matches and the null backend
opened. The run reported
`miniaudio: frames=400 read=400 rate=8000 channels=1 backend=14` (14 is
`ma_backend_null`).

One integration detail: miniaudio's bundled FLAC decoder does not compile
cleanly with this compiler at `-O0`, so the unused codecs are compiled out with
`MA_NO_FLAC`, `MA_NO_MP3`, `MA_NO_VORBIS`, and `MA_NO_OPUS` rather than patching
the dependency. The engine only decodes WAV here.

## FreeType/HarfBuzz text (2026-09-18)

Text stays on shared ecosystem components, as the plan directs, rather than a
reimplemented font stack. `native/text_probe.h` loads a system font through
FreeType (honoring `ELISA_TEXT_FONT`, otherwise the first of a small candidate
list), rasterizes one glyph to check its bitmap and advance, and shapes "Elisa"
through HarfBuzz using the same face, checking the glyph count. FreeType and
HarfBuzz are system (Homebrew) libraries linked by the probe, so nothing is
vendored for them. The run reported
`text: font=/System/Library/Fonts/Supplemental/Arial.ttf glyph=18x23 advance=21 shaped_glyphs=5 text_advance=69.36`,
so the text stack is exercised in a headless run. No UI layout is claimed; this
is the font and shaping layer, not a widget toolkit.

## zstd package compression (2026-09-18)

`native/zstd_probe.h` compresses the actual cooked package file with zstd and
decompresses it again, requiring a byte-for-byte round trip. The ratio is
therefore a measurement of a real payload, not a synthetic buffer. On this
workstation the text package compressed from 1243 to 386 bytes
(`ratio=0.311`), which is unsurprising for base64 text and shows the mechanism
rather than promising a ratio for binary geometry. zstd is a system (Homebrew)
library, so nothing is vendored for it.

## Native asset reload (2026-09-18)

The plan prioritizes asset reload before code reload: a host must release a
representation and rebuild it from canonical data without changing identity or
accumulating resources. `native/reload_probe.h` reloads the cooked package from
disk, requires the reloaded positions, normals, and indices to match the
in-memory copy exactly, builds a mesh from the reloaded package, and removes it
again so the scene object count returns to its baseline. The run reported
`reload: positions=72 indices=36 objects_baseline=10` and the probe passed, so
reload is exercised on the native host, not just assumed.

## Sanitizers at the untrusted boundary (2026-09-18)

`scripts/run_boundary_sanitized.py` builds and runs `native/boundary_harness.cpp`
under AddressSanitizer and UndefinedBehaviorSanitizer
(`-fno-sanitize-recover=all`). The harness exercises the untrusted-boundary
libraries — ozz sampling, Recast/Detour navigation, miniaudio decode,
FreeType/HarfBuzz shaping — without a renderer, so it runs where an instrumented
graphics process is not permitted. The boundary helpers were split into a
Wicked-free `native/probe_core.h` so the harness links no Wicked library. The
run passed with no sanitizer finding:

```
ozz: root_x start=0.0000 mid=1.0000 end=2.0000
recast: polys=16 navdata=2476 path_polys=5 start=20 end=29
miniaudio: frames=400 read=400 rate=8000 channels=1 backend=14
text: font=... shaped_glyphs=5
sanitized boundary harness passed: no AddressSanitizer or UBSan finding
```

The full graphics probe under the same sanitizers remains blocked in this shell:
`scripts/cxx_sanitize.py` builds it and ASan initializes, but the instrumented
graphics run aborts with no sanitizer report (the environment logs "Checking
file existence is not allowed under sandbox" and the run dies after the worker
threads start). So the boundary libraries are sanitized clean and the graphics
probe is not claimed.

## Tracy profiling client (2026-09-18)

The plan lists Tracy for early profiling once the runtime produces useful
timing data, and the probe already measures frame time, churn, and submitted
bytes. `scripts/fetch_tracy.py` pins Tracy v0.14.1 by commit
(`30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c`), and the build compiles
`public/TracyClient.cpp` with `-DTRACY_ENABLE` into the probe. The main frame
loop is instrumented with `FrameMark`, and `native/tracy_probe.h` marks a few
frames and a zone. The run reported `tracy: frames marked=4`, and the frame
determinism check still passed, so the client is compiled in and active and does
not perturb the rendered frame. With no Tracy server attached the marks are
dropped, so this is evidence the client is linked and exercised, not a captured
profile; the value is that a server can now be attached to a real run.

## Real socket transport boundary (2026-09-18)

`native/udp_probe.h` sends a 33-byte payload — the same frame size as
`src/net/wire.elisa` — across an actual UDP socket bound to loopback and
requires the bytes to arrive unchanged. That moves the transport from
"in-memory loopback only" to a real OS byte boundary, and it runs under the
sanitized boundary harness as well as the graphics probe:

```
udp: port=63584 bytes=33 round_trip=ok
```

It is a stand-in for the GameNetworkingSockets transport the plan selects, so
the status document lists GNS as still planned: this proves the boundary the
replication layer sits above, not the selected library's reliability, ordering,
or encryption features.

## Measured data layout (2026-09-18)

The plan asks data-layout choices to be measured rather than asserted.
`native/layout_probe.h` creates 512 entities and measures two ways to read their
transforms: iterating the component's contiguous storage, and looking each
entity up by id. Both medians are recorded and the contiguous walk must be no
more than a loose multiple of the indexed one. On this workstation the
contiguous walk took 1 us against 14 us indexed for the same 512 entities, the
expected cache behavior. It is one recorded comparison on one machine, not a
claim that one layout always wins; the point is that the number is recorded.
