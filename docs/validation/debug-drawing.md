# Debug drawing validation

The Elisa debug-geometry contract builds bounded collision boxes from gameplay
rules. `native/debug_draw_bridge.h` is the Wicked boundary for scoped boxes and
lines, and copied text: it rejects non-finite or inverted bounds, caps queued
commands, submits depth-tested overlays through Wicked's debug renderer, and
clears the queue after an explicit flush.

The native SDL3/Wicked gate exercises queueing, validation, renderer submission,
and scope clearing after the captured frame. Debug commands therefore cannot
change frame-determinism or topology evidence for the gameplay render. Picking
continues to use the generation-checked physics query bridge; selection outlines
and end-to-end picking overlays remain open work.
