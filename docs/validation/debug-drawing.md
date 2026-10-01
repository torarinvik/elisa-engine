# Debug drawing validation

The Elisa debug-geometry contract builds bounded collision boxes from gameplay
rules. `native/debug_draw_bridge.h` is the Wicked boundary for scoped boxes and
lines, and copied text: it rejects non-finite or inverted bounds, caps queued
commands, submits depth-tested overlays through Wicked's debug renderer, and
clears the queue after an explicit flush. The public `RenderScene` extension
(`src/runtime/render_scene_debug.elisa`) keeps the native functions private and
exposes checked `debug_box`, `debug_line`, `debug_text`, `debug_flush`, and
`debug_clear` operations to ordinary Elisa code. A render-path hook flushes any
pending commands before Wicked renders, so a caller can keep the commands inside
a short block without a persistent allocation or an exposed native queue.

`test/render_scene_debug_native.elisa` is wired into render-scene smoke group
231. It checks a valid box, line, copied text, explicit flush count, clear-after-
flush behavior, inverted bounds, and an out-of-range position. The SDL3/Metal
gate passes these cases through the real Wicked debug renderer and still checks
the existing frame and resource invariants.

`test/render_scene_debug_native_main.elisa` runs that contract in a focused
hidden SDL3/Metal scene. It saves a frame with debug drawing cleared, submits a
box, two lines, and depth-tested text, then saves the visible frame. The native
smoke decodes both 640x400 PNGs and requires at least 64 changed pixels. On
macOS 27, the focused gate passed with 778 changed pixels before batched lines
and 1052 after the test added a 2418-line heat-coloured trail ring.

Batched lines go through `elisa_render_scene_v1_debug_line_chunk`. Each call carries
up to 256 records of 10 floats: two points and an RGBA colour. `RenderScene::debug_line_records`
splits an `animation::overlay_lines` record array into chunks. The bridge accepts a chunk
only if every value is finite and all of its lines fit in the 16384-line batch; otherwise
it queues nothing. The native test fills the batch exactly, then checks that one more line
reports Capacity. It also checks that a flush returns 16384, that NaN and ragged records are
rejected as InvalidValue, and that an empty flush returns 0. The Wicked probe covers the same
all-or-nothing rule in C++. Run it with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" \
WICKED_ROOT="../amazing-labyrinth-wickedengine" \
WICKED_BUILD="../amazing-labyrinth-wickedengine/build-elisa-sdl3" \
ELISA_RENDER_SCENE_DEBUG_ONLY=1 \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

The full render-only SDL3/Metal sweep passed the debug-pixel check and all High
and Low post-process references. High uses the temporal-AA edge filter described
in [`postprocess-quality.md`](postprocess-quality.md).

The native SDL3/Wicked gate exercises queueing, validation, renderer submission,
and scope clearing after the captured frame. Debug commands therefore cannot
change frame-determinism or topology evidence for the gameplay render.

The public selection extension keeps the same boundary for editor-facing use:
`RenderScene::set_pick_identity` binds a positive gameplay epoch/ID to an
instance, `pick` returns that reference and hit distance from an authored ray,
and `select`/`clear_selection` toggle the real Wicked material outline. Native
entity IDs never cross into Elisa. Bindings are bounded and are explicitly
removed on instance destruction or scene reset.

`test/render_scene_selection_native.elisa` is wired into render-scene smoke group
232. It checks a transformed sphere hit after an owner-frame update, an off-target
miss, a zero-direction rejection, a real material outline enable, and an explicit
outline clear. The backend-neutral editor orbit camera and retained world-selection overlay
are implemented in Elisa, and `RenderScene::pick_and_select` connects one ray
hit to its gameplay identity and exact native outline. `ApplicationInput` now
queues SDL3 pointer motion, buttons, wheel changes, focus loss, and overflow;
`EditorViewport` consumes them in arrival order, scales logical window
coordinates by the active display scale, and routes clicks, right-drag orbit,
middle-drag pan, and wheel zoom. Its focused SDL3/Metal smoke injects native SDL
events, checks event translation and viewport controls, selects through
framebuffer pixels, verifies the corresponding Wicked outline and visible
overlay, rejects an undersized view without changing the existing selection,
and checks miss cleanup and destruction.

Batched labels: the SDL3/Metal smoke test queues 200 labels, past the 128
command slots. It then checks that each of these is rejected whole:
- a zero byte;
- a 64-byte label;
- an empty label;
- a NaN anchor;
- text shorter or longer than the byte counts add up to;
- too few or too many anchors.

It also checks the 1,024-label capacity (968 queued, then `Capacity`). After one
warm-up frame, 24 labels, `j00` to `j23`, are readable on the ring in the
visible capture: 1,193 changed pixels, against 1,052 without labels. The Wicked
probe checks the same rejections on the bridge, directly. Mutating the
text-total and anchor-count checks fails the smoke test with codes 43 and 45.
Mutating the length checks passes, because the native side rejects those labels
too.

## Handedness (2026-10-01)

Debug boxes, lines and text anchors now pass through
`probe::coordinates::to_wicked` in native/render_scene_debug_abi.inc, like
meshes and pick rays; before this they reached Wicked unconverted and drew
mirrored in x. Group 231 checks it with a magenta patch at Elisa x 1..3: the
right side of the frame (x_permille 620) must change and the mirrored left
side (380) must not. Cases: 46 camera/pump, 47 queue, 48 probe, 49 right side
unchanged, 50 left side changed. Negative control: passing the unconverted
chunk to `line_batch` again makes the focused debug smoke exit 49. The
skeleton display test, which had placed its skeleton at negative x to
compensate, now uses the panel's real Elisa x.

## Disabled debug drawing allocates nothing (2026-10-01)

Test builds (`ELISA_RENDER_SCENE_TEST_PROBE`) replace the global
`operator new` with a malloc forwarder that counts allocations made while a
thread-local debug scope is armed (native/render_scene_debug_alloc_probe.inc).
The render path arms it around its debug flush step, and `debug_flush` and
`debug_clear` arm it too. Group 231 then runs 60 frames with nothing queued,
calling `debug_flush` and `debug_clear` each frame, and requires the count to
stay unchanged. Cases: 51 the counter missed a known heap string (sanity
control), 52 an idle frame failed, 53 idle frames allocated. Negative control:
a `clear()` that shrinks and re-reserves its line batch every frame makes the
focused debug smoke exit 53. The claim covers the debug path only; the rest of
Wicked's frame is not measured here.

## Checked world picking (2026-10-01)

`WorldPicking::pick(world, ray, mask)` (src/runtime/world_picking.elisa)
returns `Hit` with a `World::EntityRef` only when the render hit carries the
world's epoch and `world_is_live` still holds the entity. A hit from another
world or on a despawned entity is `Stale`; no hit is `Miss`; other render
failures raise `InvalidValue`. `bind_pick_identity` stamps an instance with
the entity reference. The verdict is `WorldPickCheck::classify`
(src/world/pick_check.elisa); proof/world_pick_check.elisa proves its truth
table, 45 of 45 obligations. Removing the "not live" branch makes the proof
fail. The `world-picking-smoke` native application smoke checks it on a live
SDL3/Wicked box. Cases: 30 hit verdict, 31 hit entity, 32 miss, 33 another
world not stale, 34 despawned entity not stale, 35 despawn failed, 36 a
zero-direction ray not reported as an error. Negative control: resolving
liveness from the hit alone makes the smoke exit 34.

## Bounds in one view (2026-10-01)

`BoundsView` (src/tooling/bounds_view.elisa) gathers three bounds in Elisa
world coordinates: `RenderScene::instance_bounds` (Wicked object AABBs mapped
with `from_wicked`), `PhysicsRuntime::body_bounds` (the Jolt shape's local box
turned and moved by the body pose, then `from_wicked`) and `NavMesh::bounds`
(the ground polygons of a live mesh; a stale handle is refused). `agree` checks
every face within a tolerance, `covers_footprint` checks that one box covers
another in x/z, and `draw` submits all three through `RenderScene::debug_box`
in blue (render, depth tested), orange (physics) and green (nav). The scalar
comparisons are `BoundsCompare::within` and `covers`; proof/bounds_compare.elisa
proves them, 9 of 9 obligations, with no semantic diagnostics; both reduce
to `gaps`, which checks two differences against a named `ZERO` (the static
checker discharges preconditions only against named consts). Dropping either
gap guard makes the proof fail. Body bounds support box, sphere, capsule and cylinder
shapes; other shapes raise InvalidArgument. `Primitive.Box` has half extent 1,
so a render box matching a 0.5 half-extent body uses scale 0.5.

The `bounds-view-smoke` native application smoke puts a box at x 1.5 on a
navmesh slab. Cases: 40 gather failed, 41 render and physics disagree, 42 the
render box is not at +x, 43 nav does not cover the body footprint, 44 the nav
ground is not at the body's base, 45 a body moved to x 3 still agrees, 46 draw
failed, 47 pump failed, 48 an unloaded nav handle still reports bounds.
