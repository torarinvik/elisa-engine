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
extend beyond the framebuffer are rejected. `Camera::bounds_visible` also tests
a world-space AABB against the transformed perspective or orthographic frustum;
it builds a conservative camera-space box so camera rotation and nonuniform
scale cannot cull visible geometry. The portable test covers near/far and
side-plane cases. `RenderScene::set_camera_viewport` configures a secondary
camera to render through Wicked's existing per-camera render-to-texture pass
and composites its image into a checked framebuffer-pixel rectangle over the
primary view. The viewport dimensions also set that camera's render resolution
and aspect ratio; a bounded update interval can throttle it. Clearing the
viewport releases the target and stops composition. The SDL3/Metal smoke
rejects a rectangle extending beyond the framebuffer, then renders and
composites a valid right-half view and verifies the target and composition
count before checking that cleanup stops composition. A viewport camera cannot
become the full-screen active camera until its viewport is cleared; clearing
restores the camera's saved projection dimensions.

The Wicked gate runs `native/camera_bridge.h`, which creates perspective and
orthographic camera components, applies a 2x viewport scale, resizes a
perspective view, switches an actual `RenderPath3D` between the two cameras,
rejects a missing camera without disturbing the active view, and removes both
temporary views without retaining native camera entities. The project-facing
`RenderScene::camera_ray` ABI unprojects the active Wicked camera in reverse-Z
space, reflects the result back into Elisa's right-handed coordinates, returns
camera origins for perspective and near-plane origins for orthographic views,
and rejects pixels outside the viewport. `RenderScene::camera_viewport_ray`
does the same for a secondary view after translating global framebuffer
coordinates into its viewport; points outside the rectangle fail. The
SDL3/Metal smoke checks centered rays and out-of-bounds requests for active and
secondary views.

When the framebuffer resizes, each configured secondary viewport scales its
pixel rectangle by the old-to-new framebuffer ratio, updates its render target
resolution, and recreates its projection with the new aspect ratio. The native
camera test grows from 320x200 to 640x400 and verifies that a (160, 0, 160,
100) viewport becomes (320, 0, 320, 200), still composes, and still returns a
finite center ray at (480, 100). Resizing back restores the original rectangle
and target dimensions.

Automatic static snapshot LOD selection now checks the active Wicked frustum
against the transformed bounds of every mesh placement. It leaves the current
shared mesh untouched while all placements are offscreen, then selects the
appropriate level on the first visible frame. The native asset smoke verifies
both the deferred offscreen case and the visible transition. The
camera-component pass supplies a lightweight secondary view. Dedicated
`RenderPath3D` pipelines per camera, with independent full post-processing and
primary-view layout, remain open; Wicked continues to cull scene draws through
its own visibility path.
