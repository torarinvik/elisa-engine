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
