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

- **The skinned mesh is not drawn into this texture.** The Wicked renderer draws only into its SDL swapchain, and there is no Wicked render-to-IOSurface path. For now the viewport draws engine draw lists: grid, skeleton and overlays. Routing Wicked's scene render into the ring needs a native change in `render_scene_abi.cpp`, and it cannot be validated headlessly because Wicked needs its window.
- **Several viewports sharing one scene.** Each `Viewport` is independent (its own device queue and ring). Three viewports work side by side, but there is no shared-scene object yet.
- **On-screen compositing inside elisa-ui.** This is added in the elisa-ui `mocap-viewport` worktree; see that branch's notes.
