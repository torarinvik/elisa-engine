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
menu/state half of Phase 4 made executable without a UI toolkit; audio
cues remain data the game emits but no host plays yet, and packaging for
the validated targets is still outstanding.

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

## Fog of war in the hosts (2026-09-18)

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
