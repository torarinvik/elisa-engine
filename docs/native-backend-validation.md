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
