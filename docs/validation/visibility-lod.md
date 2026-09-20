# Visibility and LOD validation

`native/visibility_lod_bridge.h` keeps visibility policy at the Elisa/native
boundary. It validates draw distance and LOD bias, applies layer and renderable
state to real Wicked objects, exposes the renderer's occlusion-culling switch,
and rejects foreign or retired handles.

Evidence: the SDL3/Wicked gate binds a real cube, checks distance/LOD/layer
updates, toggles renderability and occlusion culling, rejects a foreign handle,
and verifies removal. Elisa screen-error selection and large-scene batching are
still open R10 work.
