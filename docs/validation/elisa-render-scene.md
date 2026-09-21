# Elisa render scene service

`src/runtime/render_scene.elisa` connects Elisa-authored application code to a
Wicked `Scene` and `RenderPath3D` without exposing a Wicked type or entity ID.
The application initializes first; then `RenderScene::initialize` creates the
engine-owned scene, active orthographic camera, and render path. Elisa creates
box, sphere, and plane primitives with a `Geometry::Transform` and per-instance
RGBA color, then updates full position/rotation/scale, color, or visibility and
destroys them using opaque generation-checked handles.

The service accepts at most 256 live instances. Position and quaternion
components, camera vectors, and positive scales are limited to a magnitude of
1,000,000 world units; orthographic height ranges from 0.001 to 1,000,000.
Quaternions must be non-zero and are normalized; colors must be finite within
0–1; look-at direction and up vectors must be non-degenerate. Calls run on the
thread that initialized the application, and wrong-thread initialization is
rejected before touching Wicked scene state.
`RenderScene::set_camera_perspective(vertical_fov_radians, near_clip, far_clip)`
selects Wicked's perspective projection after validating the radian field of
view and clipping range. `RenderScene::resize` preserves the active projection
and its settings; `set_camera_orthographic_height` switches back to orthographic
mode. Applications should pass the pixel extent reported by
`Application::frame_info` when the window changes size. The initial camera is
orthographic, centered over the origin with +Y up, and game code can set its
pose with `RenderScene::set_camera_look_at`.

`RenderScene::sync_snapshot` connects `RenderSnapshot::Snapshot` to this scene
service as one bounded native transaction of at most 256 rows. Each row carries
the checked gameplay epoch and entity ID, stable render ID, mesh and material
asset IDs, and transform. The native boundary validates the staged rows and
handles, creates new or asset-replacement objects before touching the current
frame, and removes those temporary objects if any creation fails. It then
applies retained transforms and retires removed or replaced handles, returning
new opaque handles only after commit. Presenter storage remains fixed-capacity
inside `RenderScene`. Clear the presenter before an explicit scene shutdown;
application shutdown releases all native scene resources automatically.

The native scene records gameplay and asset identity with each snapshot-owned
instance and reuses a handle only when its mesh and material IDs are unchanged.
The snapshot adapter still renders default boxes; it does not resolve those IDs
to cooked packages or material descriptors. That loader integration belongs to
A03/A04. Gameplay transforms live in each live `World` registry row and are
validated before assignment. A render binding carries only a per-render local
attachment transform; extraction composes it with the entity transform, so
one entity can own multiple visual parts without copying its world pose into
each binding.

`RenderScene::set_texture` attaches a project-relative base-color, normal,
packed surface, or emissive image to one live instance. The engine canonicalizes
the path beneath `ELISA_PROJECT_ROOT`, loads it through Wicked's resource
manager with block compression, and requests Wicked's normal-map import format
for the normal slot. Wicked's surface channels are occlusion, roughness,
metalness, and reflectance in RGBA order. The API does not change the material
shading model or discover companion maps from FBX metadata; an Elisa project
currently selects each map explicitly. The cooked mesh format retains UVs and
validated tangent frames generated after simplification, so a loaded normal map
can shade through Wicked's PBR material path.

`src/runtime/world_rendering.elisa` adds an Elisa-owned `WorldRendering`
binding table and extractor. Callers bind one or more stable render IDs and
local attachment transforms to a checked `World::EntityRef`; gameplay code
sets the entity's world transform through `World::world_set_transform`.
Extraction verifies world liveness, composes both transforms, rebuilds a render
snapshot, and prunes references after despawn or world replacement. The
portable fixture covers one-to-many bindings, world-transform updates, local
offsets, invalid scales, duplicate IDs, foreign-world references, capacity,
and stale-row cleanup. The native scene smoke now spawns real `World` entities, extracts
their render rows, syncs twice to verify handle reuse, and despawns them to
verify native retirement. It also changes an asset ID, injects failure after
one replacement has been created, verifies the prior frame and identity
metadata remain intact, and retries the complete update successfully.

`RenderScene` also exposes generation-checked `ElectricArcHandle`s and a bounded
`ElectricArcBatch` of at most 1,024 arcs. Elisa supplies endpoints, phase, and
visibility; Wicked owns the endpoint-pinned additive core and halo trails and
may add a short branch. `DrawTrailRuntime` submits these game effects even when
Wicked debug drawing is disabled. The native smoke checks invalid dimensions,
degenerate endpoints, batch and native-slot overflow, hide/show rendering,
destruction, and stale-handle rejection. A test-only GPU readback hashes the
center 17-by-17 pixel patch to verify that showing the arc changes the rendered
image; ordinary game builds do not expose this probe. Visual tuning and socket
alignment in authored gameplay remain open.

Calling `RenderScene::shutdown` releases the render path and all owned scene
resources. The engine also registers a shutdown hook: application shutdown
detaches the active path, then releases the scene before Wicked tears down its
graphics device. Stale handles are rejected, including after explicit
destruction or scene replacement. No scene state or rendering survives
application shutdown.

Validation: `scripts/render_scene_native_smoke.py` builds an ordinary Elisa
`main()` with no game-authored exports or C++ source, launches a real hidden
SDL3/Metal window from a working directory containing spaces, and calls the
public application and render-scene modules. It cooks a synthetic FBX triangle,
loads it through `RenderScene::create_mesh`, and verifies that the cooked mesh
alone changes the rendered image. Traversal, absolute paths, and a symlink
escaping the project root are rejected. It also checks invalid dimensions,
degenerate and out-of-range camera inputs, malformed transforms/colors, stale
handles across scene replacement, orthographic and perspective camera changes,
invalid perspective field-of-view and clipping inputs, projection-preserving
resize and return to orthographic mode, both explicit and application-triggered
cleanup, real snapshot submission, stable-handle reuse,
transform updates, removed-row retirement, and that the rendered center pixel
differs from the clear corner. The native pixel probe waits up to 400 ms for
asynchronous pipeline creation, waits for GPU work, and reads the `RenderPath3D`
offscreen target rather than the swapchain backbuffer. The bounded frame retry
still handles a slow first Metal frame. That blocking readback probe is
compiled only for this smoke; regular application builds do not expose it from
the render ABI. The smoke requires the pinned macOS
SDL3/Metal Wicked libraries; the Elisa module itself can also be compiled
without those native dependencies. `elisascript scripts/check.elisascript`
passes the combined persistent-snapshot and world-rendering portable fixture.
Evidence for commit `f2266fd` on 2026-09-20:

- `elisascript scripts/check.elisascript` — passed the portable, Godot, and Elisa Proof suite, including world extraction.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/render_scene_native_smoke.py` — passed against SDL3/Metal/Wicked with live Elisa `World` entities.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` — passed with the public runtime bundle including `WorldRendering`.
- `python3 scripts/check_source_length.py` and `python3 scripts/check_module_hygiene.py` — passed.

Follow-up after the macOS 27.0 update: the native pixel query now waits for
Wicked's asynchronous pipeline creation and compares the offscreen render-path
target. `DEVELOPER_DIR=/Library/Developer/CommandLineTools elisascript
scripts/wicked_probe.elisascript build` passed, including the generic
application lifecycle smoke and this rendered-scene smoke.

Mesh follow-up, 2026-09-20: `DEVELOPER_DIR=/Library/Developer/CommandLineTools
python3 scripts/render_scene_native_smoke.py` passed with cooked triangle
rendering and project-root path rejection. `python3 scripts/test_elisa_build_run.py`,
`python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`,
and `python3 scripts/cook_assets.py "$PWD" && elisascript scripts/check.elisascript`
passed.

Snapshot transaction follow-up, 2026-09-20: the full Elisa suite passed, including
the portable snapshot tests and both proof suites (17/17 and 6/6). The SDL3/Metal
scene smoke passed after forcing a native entity-creation failure midway through
a changed-asset batch, checking that the native object count and previous asset
identity remain intact, then retrying and verifying update and despawn. The
ordinary application smoke passed both lifecycle binaries. Source-length,
module-hygiene, dependency-manifest, and `git diff --check` checks passed.

World-transform follow-up, 2026-09-20: the full Elisa suite passed with checked
TRS values stored in `World` and render-local offsets composed during extraction.
The SDL3/Metal scene smoke passed world transform assignment, entity fan-out,
asset replacement rollback/retry, and despawn retirement after this change.
Source-length, module-hygiene, native dependency-manifest, and
`git diff --check` checks passed.

This is an initial renderer. It owns one active scene and a configurable
orthographic or perspective camera, uses unlit colors unless emission selects
PBR shading, and loads static cooked geometry packages for authored snapshots.
Texture assignment is per instance; complete imported material descriptors
remain future work.
Entity world transforms are stored in `World`; only per-render local attachment
offsets live in the render binding table. The general `InstanceBatch` remains
an Elisa-side collection of checked handles and does
not batch arbitrary renderer calls; snapshot reconciliation has its own
transactional native submission. The renderer does not yet expose shared mesh
residency, FBX material mapping, parenting, general lighting, or editor tooling.

Maze-world native integration follow-up, 2026-09-21: the native scene smoke now
includes `test/maze_rendering_native.elisa`. It constructs the maze presentation
from public topology and game state, submits the 40-row initial snapshot through
the transactional `SnapshotPresenter`, plays the winning trace, checks key
removal and the opened-door material, verifies restart restores 40 native
instances, and clears the presenter back to zero instances. Validation passed:
`DEVELOPER_DIR=/Library/Developer/CommandLineTools
ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh"
python3 scripts/render_scene_native_smoke.py` on the SDL3/Metal Wicked backend.
The interactive client has since moved to `examples/maze/native_main.elisa`:
gameplay entities are built in Elisa `World`, snapshots update the persistent
native scene, and keyboard input drives the maze rules. It does not read the
diagnostic manifest. The finite `native_smoke_main.elisa` entry now runs as part
of `scripts/render_scene_native_smoke.py`, checking the SDL3/Metal win, restart,
entity counts, transactional clear, and orderly shutdown. Both ordinary project
build and hidden native smoke passed after the macOS 27.0 update. Authored cooked
mesh/material resolution and a manual visible controls check remain open; until
then the game entities render as placeholder boxes.

Electric arc follow-up, 2026-09-20: Wicked commit `c1c9300` adds a runtime trail
queue that is independent of debug drawing. Engine commit `bef49cc` integrates
bounded Elisa arc handles and records invalid-input, 1,024-slot capacity,
visibility, rendered-patch change, destruction, and stale-handle checks.
`elisascript scripts/check.elisascript` passed. The SDL3/Metal scene smoke
passed with `DEVELOPER_DIR=/Library/Developer/CommandLineTools
ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh"
python3 scripts/render_scene_native_smoke.py`. The full native gate command
`DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=
"../Elisa-compiler/scripts/elisac_stage1.sh" elisascript
scripts/native_gate.elisascript native` passed every stage on macOS 27.0 / Apple
M5 with `hardware_verification=verified`; [`build/native-gate.json`](../../build/native-gate.json)
records the `bef49cc338a4bb483e910003f3a3b773b33bf445` source revision.

## Art-direction APIs (2026-09-21)

`RenderScene::set_lit_material(instance, roughness, metallic)` opts a primitive or
cooked mesh into Wicked PBR lighting and shadow casting. Both scalars must be
finite and in [0, 1]; surface-map channels multiply them. Legacy instances remain
unlit until opted in. `set_sun_shadows(enabled)` controls the environment sun and
render-path shadows; call it after `set_environment` (which resets sun shadows).

`set_texture_uv_transform(instance, scale_u, scale_v, offset_u, offset_v)` sets a
finite material UV transform for all maps. FBX exports using a bottom-left V
origin can use `(1, -1, 0, 1)` to align maps in Wicked. This does not modify source
meshes or automatically identify their UV convention.

`set_electric_arc_depth_test(arc, enabled)` enables physical occlusion and applies
to core, halo and branches, persisting through updates. Existing arcs keep their
overlay default. Orthographic scene arcs use hardware depth without the legacy
perspective soft fade. The Wicked TrailRenderer change allows zero depth-soften
to disable soft fading independently of hardware depth and avoids division by
zero. No game-authored native code is required.

Native smoke coverage checks lit material state, UV transform, shadow-casting
flags, invalid roughness, arc depth toggling, and rejection of a retired arc
handle. The Amazing Labyrinth art study supplies native visual evidence.
