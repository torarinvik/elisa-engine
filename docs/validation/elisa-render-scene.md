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
`RenderScene::resize` updates the orthographic projection and preserves the
current view; applications should pass the pixel extent reported by
`Application::frame_info` when the window changes size. The initial camera is
centered over the origin with +Y up, and game code can set its pose with
`RenderScene::set_camera_look_at`.

`RenderScene::sync_snapshot` connects `RenderSnapshot::Snapshot` to this scene
service. It reconciles at most 256 Elisa rows by stable render ID, updates
existing handles in place, creates default boxes for new rows, and retires IDs
absent from the next snapshot. Presenter storage is fixed-capacity and remains
inside the `RenderScene` module so native handles stay opaque. Clear the
presenter before an explicit scene shutdown; application shutdown releases all
native scene resources automatically.

`src/runtime/world_rendering.elisa` adds an Elisa-owned `WorldRendering`
binding table and extractor. Callers bind one or more stable render IDs and
transforms to a checked `World::EntityRef`; extraction verifies world liveness,
rebuilds a render snapshot, and prunes references after despawn or world
replacement. The portable fixture covers one-to-many bindings, transform
updates, duplicate IDs, foreign-world references, capacity, and stale-row
cleanup. The native scene smoke now spawns real `World` entities, extracts
their render rows, syncs twice to verify handle reuse, and despawns them to
verify native retirement.

Calling `RenderScene::shutdown` releases the render path and all owned scene
resources. The engine also registers a shutdown hook: application shutdown
detaches the active path, then releases the scene before Wicked tears down its
graphics device. Stale handles are rejected, including after explicit
destruction or scene replacement. No scene state or rendering survives
application shutdown.

Validation: `scripts/render_scene_native_smoke.py` builds an ordinary Elisa
`main()` with no game-authored exports or C++ source, launches a real hidden
SDL3/Metal window from a working directory containing spaces, and calls the
public application and render-scene modules. It checks invalid dimensions,
degenerate and out-of-range camera inputs, malformed transforms/colors, stale
handles across scene replacement, camera resize, both explicit and
application-triggered cleanup, real snapshot submission, stable-handle reuse,
transform updates, removed-row retirement, and that the rendered center pixel
differs from the clear corner. The native pixel probe waits up to 400 ms for
asynchronous pipeline creation, waits for GPU work, and reads the `RenderPath3D`
offscreen target rather than the swapchain backbuffer. The bounded frame retry
still handles a slow first Metal frame. The smoke requires the pinned macOS
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

This is an initial generic primitive renderer. It owns one active scene and
orthographic camera, uses unlit colors, and maps snapshot rows to default boxes.
Transforms are currently stored in the separate render binding table rather
than extracted from a general world transform component. `InstanceBatch` is an
Elisa-side collection of checked handles and does not batch renderer calls. The
renderer does not yet expose authored mesh or texture loading, parenting, a
single transactional native batch submission, lighting, or editor tooling.
