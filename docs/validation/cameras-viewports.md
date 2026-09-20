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
orthographic origins, and forward direction. Render-to-texture and multi-camera
scheduling remain native R03 work.

The Wicked gate runs `native/camera_bridge.h`, which creates perspective and
orthographic camera components, applies a 2x viewport scale, resizes a
perspective view, and removes both temporary views without retaining native
camera entities. Render targets, camera switching, and frustum scheduling are
still higher-level work.
