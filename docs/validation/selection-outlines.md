# Selection outline validation

`native/selection_outline_bridge.h` translates a checked selected object into
Wicked material outline flags and enables the active `RenderPath3D` outline
pass. It records bounded material subsets, clears every flag on replacement or
release, and keeps the native selection state separate from Elisa gameplay IDs.

Evidence: the SDL3/Wicked gate selects a real cube material, verifies the
outline pass and material flag, clears the selection, and verifies cleanup.
Editor-owned end-to-end selection overlays remain open R06 work.
