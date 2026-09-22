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

The native SDL3/Wicked gate exercises queueing, validation, renderer submission,
and scope clearing after the captured frame. Debug commands therefore cannot
change frame-determinism or topology evidence for the gameplay render. Picking
continues to use the generation-checked physics query bridge; selection outlines
and end-to-end picking overlays remain open work.
