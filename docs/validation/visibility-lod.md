# Visibility and LOD validation

`native/visibility_lod_bridge.h` keeps visibility policy at the Elisa/native
boundary. It validates draw distance and LOD bias, applies layer and renderable
state to real Wicked objects, exposes the renderer's occlusion-culling switch,
and rejects foreign or retired handles.

`RenderSceneVisibility` exposes the same policy through the public
generation-checked `RenderScene::InstanceHandle`. `VisibilityPolicy` is a
private-field constructor value, so callers can retain policy data without
exposing native objects; `set_visibility` validates it at the native boundary
and `set_occlusion_culling` controls the active Wicked render path.

Evidence: the SDL3/Wicked gate binds a real cube through the public Elisa API,
checks distance/LOD/layer updates, rejects a zero draw distance, toggles
renderability and occlusion culling, and verifies the applied Wicked state.
Elisa screen-error selection and large-scene batching are still open R10 work.
