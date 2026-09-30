# Embeddable viewport through a shared Metal texture (M01)

Date: 2026-09-30. Branch: `mocap-track`. Reference machine: Apple M5, macOS 27.

## Design

The engine owns each viewport's render target. A UI toolkit shows it and never draws into it.

- **Target.** `native/viewport_metal.m` keeps a ring of 3 IOSurface-backed BGRA8 `MTLTexture`s plus one Depth32 texture per viewport. It needs a Metal device, not a window, so it runs headless.
- **Hosting.** The host sets a `CALayer`'s `contents` to the published `IOSurfaceRef`, so it composites with zero copies. The IOSurface id (`published_surface_id`) lets another process look the surface up.
- **Frame sync** (`ViewportPolicy`, proved):
  - Frame `f` renders into slot `f mod 3`.
  - The producer may begin frame `f` only when `acked < f <= acked + 2`. `acked` is the newest frame the host reports on screen.
  - So the slot the host is showing, and the one queued behind it, are never overwritten.
  - Without acknowledgements, the producer stops after two frames and keeps its dirty bits.
  - An acknowledgement newer than the published frame is ignored, and so is one at or below `acked`.
- **Lifetime.** `create` allocates the ring (generation 1). `destroy` frees it, after which `tick` raises `Destroyed` and `published_surface` is 0. Surfaces are borrowed: a host that keeps one past the next `tick` must `CFRetain` it.
- **Resize.**
  - `resize` with the same size does nothing.
  - A new size reallocates the ring and bumps `generation`. It also unpublishes: `published_surface` is 0 until the next `tick`.
  - Sizes are clamped to [1, 16384], so a collapsed panel keeps a 1×1 texture.
  - When the host sees a new generation, it must drop its references to the old surfaces.
- **Redraw on demand.**
  - `invalidate(DIRTY_CAMERA | DIRTY_SCENE | DIRTY_SIZE | DIRTY_OVERLAY)` marks the viewport dirty; invalid masks are ignored.
  - `tick` renders only when the viewport is dirty or `playing`. Otherwise it is one branch.
  - Rendering is synchronous: the command buffer has completed when `tick` returns true.
- **Camera** (`ViewportCamera`):
  - perspective, plus orthographic front and side views;
  - orbit, with pitch clamped to ±89° and yaw wrapped; orthographic views refuse to orbit;
  - pan, which keeps the grabbed point under the cursor;
  - zoom in exponential notches, with distance doubling every 8 notches;
  - `frame_points`, which fits a point set with a pixel margin in every projection;
  - `ray`, which gives picking rays, parallel in the orthographic views.
  - Integer policy (pitch/zoom codes, yaw wrap) lives in `ViewportPolicy`.
- **Draw lists** (`ViewportDraw`):
  - lines and triangles in a depth-tested layer and in a TOP layer (x-ray overlays);
  - a floor grid with a red X axis and a blue Z axis;
  - screen-constant joint markers, and bones.
  - Non-finite input, a bad layer or more than 4M vertices raise a typed `DrawError` and leave the list unchanged.
- **CPU reference** (`ViewportRaster`) renders the same list with the same matrix. It serves as the oracle and as a headless thumbnail path.
- **PNG** (`PngWriter`): RGBA8, stored deflate, deterministic.

## Proof

```
/private/tmp/claude-501/mc/elisa-proof "$PWD/proof/viewport_policy.elisa"
```

All 123 obligations are proven, 0 fail, and the certificate replays with no gaps. The proofs cover:
- extent validity and clamping;
- generation increase;
- pitch/zoom code clamps and yaw wrap staying in range;
- ring slots staying in [0, 3);
- `can_begin`: never more than two frames past `acked`, never at or behind it;
- idle ticks never rendering, playing always rendering, and mask validity.

The prover has no floats, so the float camera maths is covered by tests instead.

## Tests

```
ELISA_ALLOW_STALE_STAGE1=1 elisac-stage1 -emit exe -o build/viewport_camera-test test/viewport_camera.elisa && ./build/viewport_camera-test
ELISA_ALLOW_STALE_STAGE1=1 scripts/build_viewport_native_test.sh viewport_render && ./build/viewport_render-test
```

Both are registered in `scripts/check.elisascript`. The second is linked by `scripts/build_viewport_native_test.sh`, which links `native/viewport_metal.m`, the engine's native fallbacks and the runtime object with `-framework Metal -framework IOSurface -framework Foundation`.

- **`viewport_camera`:**
  - projection centre and orientation, and points behind the eye;
  - ray/project round trips over 20 points in each of the 3 projections;
  - orthographic rays parallel and independent of depth;
  - pitch clamp, yaw wrap, and huge or NaN orbit input refused;
  - zoom doubling and clamp;
  - pan keeping the grabbed point under the cursor;
  - resize clamps;
  - framing in 3 projections × 2 aspect ratios;
  - empty, single-point and NaN framing.
- **`viewport_render`:**
  - **Adversarial input:** create at 0×10, 10×20000 and −3×−3 raises `BadSize`. A NaN vertex raises `NotFinite`, layer 7 raises `BadLayer`, and a negative grid spacing is refused, all with the list unchanged.
  - **PNG:** the encoding is byte-identical twice; the signature is right and IEND's CRC is `AE 42 60 82`. A short pixel buffer and a zero size are both typed errors. Python's `zlib` decodes the written files.
  - **Redraw on demand:** the first tick renders and publishes a surface with a non-zero IOSurface id. The next 10,000 idle ticks render nothing.
  - **Idle cost:** about 245 µs for 10,000 ticks, 25 ns each, on three runs. The test fails above 50 ms.
  - **GPU vs CPU:** the Metal readback of a grid, floor patch and stick figure matches `ViewportRaster`, with 89 pixels in 10,000 differing by more than 48 in any channel. All of them are at rasterisation edges. The limit is 150. The images are `build/viewport_render_gpu.png` and `build/viewport_render_cpu.png`.
  - **Back-pressure:** with no acknowledgements, frame 2 is refused and the dirty bits are kept. A future ack (5) and a stale ack (0) are ignored. After ack 1, frame 2 renders into slot 2.
  - **Masks and playing:** an invalid dirty mask is ignored, and playing renders every acknowledged tick.
  - **Resize:** the same size keeps the generation. 400×300 bumps it and unpublishes, the next tick republishes, and the readback is 400×300×4 bytes. A 0×0 resize clamps to 1 pixel.
  - **Destroy:** after `destroy`, `tick` raises `Destroyed` and no surface is published.

## Still open

- **The backdrop is a CPU copy.** The Wicked frame goes through a readback and an upload each time it changes; a GPU-side blit from Wicked's Metal texture into the ring would remove the copy.
- **On-screen compositing inside elisa-ui.** This is added in the elisa-ui `mocap-viewport` worktree; see that branch's notes.

## Shared scene (2026-10-01)

`ViewportScene` (src/viewport/viewport_scene.elisa) holds one skeleton, selection and grid flag with a revision counter. Each `Viewport` keeps the last revision it drew; `tick` invalidates only when the revision moved and builds a draw list only when the viewport will render. `test/viewport_scene.elisa` drives a perspective view plus front and side orthographic views from one scene: each renders once, 1000 idle ticks render nothing, one scene change redraws every view exactly once, resizing the side view changes only its generation, and selecting a joint turns it orange in all three views and frame-selection centres it within 10 px. A mutant that drops the revision invalidate is caught (exit 9).

## Skinned mesh backdrop (2026-10-01)

`Viewport::set_backdrop` uploads an RGBA8 frame that every later render stretches over the target before the draw list; `clear_backdrop` removes it. The Wicked side gained `elisa_application_v1_read_rgba` with `presented_width`/`presented_height` (`Application::read_presented`), which copies the last presented frame. `scripts/viewport_backdrop_smoke.py` builds `test/viewport_wicked_backdrop_main.elisa` through the render-scene harness with `build/viewport_metal.o` linked in (new `ELISA_RENDER_SCENE_EXTRA_OBJECTS`). Wicked draws the skinned panel at 640x480; the test waits until the mesh is on screen, sets it as the backdrop of a 320x240 viewport sharing a `ViewportScene`, and checks one redraw on the backdrop change, none while idle, the orange selected joint on top, and the plain clear colour back after `clear_backdrop`. Across three runs the script finds 99.35% of viewport pixels match the Wicked frame. A mutant that skips the backdrop draw is caught (exit 28). The first version waited a fixed six frames and sometimes captured before the mesh streamed in; the wait is now on the frame content.

## Camera sync (2026-10-01)

`ViewportWicked::sync_camera` (src/viewport/viewport_wicked.elisa) copies a `ViewportCamera` to Wicked's main camera: perspective field of view and clip range, or orthographic height `2 * half_height`, then eye, target and up. The backdrop test drives Wicked from a perspective camera, the front and side orthographic cameras, and the perspective camera again after an orbit and zoom. At 25 pixels per view, Wicked's `RenderScene::camera_ray` must agree with `ViewportCamera::ray`: 1 - cos of the angle between directions, and the distance of Wicked's origin from the viewport ray, must both stay under 50e-6. The measured mismatch rounds to 0 at 1e-6. A 10% field-of-view mutant is caught (exit 41), and a 0.75x orthographic-height mutant is caught (exit 42). The first version of the check accumulated its worst value in nested `for` loops and always reported 0 even for the mutants. It now uses one `while` loop with a per-sample helper; the Elisa discarded-accumulator quirk is suspected.
