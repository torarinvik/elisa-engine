# Camera and viewport validation

`src/backend/camera.elisa` owns perspective and orthographic projection policy,
high-DPI viewport dimensions, near/far clipping validation, and picking-ray
construction. Pixel coordinates convert to normalized device coordinates before
the ray is transformed by the Elisa TRS, so a backend receives the same ray for
Wicked and Godot.

The viewport rejects zero or oversized dimensions and records display scale.
`test/camera.elisa` covers centered perspective rays, camera movement, resize
scale, orthographic origins, and forward direction. Render-to-texture and
multi-camera scheduling remain native R03 work.
