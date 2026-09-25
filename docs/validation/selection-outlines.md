# Selection outline validation

`native/selection_outline_bridge.h` translates a checked selected object into
Wicked material outline flags and enables the active `RenderPath3D` outline
pass. It records bounded material subsets, clears every flag on replacement or
release, and keeps the native selection state separate from Elisa gameplay IDs.

Evidence: the SDL3/Wicked gate selects a real cube material, verifies the
outline pass and material flag, clears the selection, and verifies cleanup.
`RenderScene::pick_and_select` now resolves the gameplay reference and outlines
the exact hit object in one owner-thread call; a miss clears the previous
outline. Group 232 verifies the returned identity, native outline state, and
miss cleanup. The retained `WorldSelectionOverlay` composes the checked pick
result into a bounded panel with world epoch and entity ID labels. The same
group verifies that the panel and text appear together, hide together, and
release their handles.
