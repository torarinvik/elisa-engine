# Picking validation

`native/picking_bridge.h` keeps native entity IDs private and maps only bound
Wicked ray hits to positive Elisa world/entity references. Bindings are bounded,
generation checked, owner checked, layer filtered, and explicitly unbound before
selection can be reused. Invalid and stale selections return no hit.

Evidence: the SDL3/Wicked gate creates a real cube, performs a filtered ray pick,
checks the returned gameplay reference and distance, rejects a foreign bridge and
layer, unbinds the selection, and removes the native target. Selection outlines
and editor end-to-end overlays remain open R06 work.
