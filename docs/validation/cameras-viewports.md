# Camera and viewport validation

`src/backend/camera.elisa` owns perspective and orthographic projection policy,
high-DPI viewport dimensions, near/far clipping validation, and picking-ray
construction. Pixel coordinates convert to normalized device coordinates before
the ray is transformed by the Elisa TRS, so a backend receives the same ray for
Wicked and Godot.

The viewport rejects zero or oversized dimensions and records display scale.
`Camera::resize_or_suspend` records a zero pixel extent as an explicit
suspended state and restores a valid viewport on the next non-zero resize, so
projection math never divides by zero. `test/camera.elisa` covers centered
perspective rays, camera movement, resize scale, suspension/resume,
orthographic origins, forward direction, and a right-half split viewport whose
global pixel coordinates map to the viewport center. Invalid rectangles that
extend beyond the framebuffer are rejected. Render-to-texture and multi-camera
render scheduling remain native R03 work.

The Wicked gate runs `native/camera_bridge.h`, which creates perspective and
orthographic camera components, applies a 2x viewport scale, resizes a
perspective view, switches an actual `RenderPath3D` between the two cameras,
rejects a missing camera without disturbing the active view, and removes both
temporary views without retaining native camera entities. Render targets and
frustum scheduling remain higher-level work. The project-facing
`RenderScene::camera_ray` ABI unprojects the active Wicked camera in reverse-Z
space, reflects the result back into Elisa's right-handed coordinates, returns
camera origins for perspective and near-plane origins for orthographic views,
and rejects pixels outside the viewport. The SDL3/Metal smoke checks a centered
ray after changing the camera and an out-of-bounds request.
