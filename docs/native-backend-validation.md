# Native backend validation

The engine's SDL3 boundary and Godot host contract are checked by the regular
ElisaScript workflow. The full Wicked renderer remains a separate native spike;
its source is kept outside this repository so upstream code and build artifacts
do not become Elisa source.

## Godot

Homebrew Godot 4.7.2 was installed on 2026-09-17. The headless probe runs with:

```sh
godot --headless --path backends/godot --script backends/godot/probe.gd
```

It applies the portable create/update/destroy lifecycle to a real Godot
`MeshInstance3D`, `StandardMaterial3D`, and `Camera3D`, then validates epoch/entity
identity, transform update, and destruction ordering. It does not claim a
GDExtension or pixel-level renderer equivalence.

## Wicked

The upstream checkout was cloned beside this repository at revision
`5e07e3bfd7f89633a468009e0620b14e508dd8b7` (2026-09-17). It is not copied into
the engine repository. The arm64 Debug/O0 CMake build produces the static
Wicked, Jolt, Utility, FAudio, and Lua libraries needed by the probe. The
release build still hits an Apple Clang frontend crash in `wiPrimitive.cpp`,
and the optional upstream `offlineshadercompiler` target needs additional
Apple framework link flags.

The repository contains a small native scene probe in
`native/wicked_probe.cpp`, driven by `scripts/wicked_probe.elisascript`. It
creates an Elisa-named cube and camera, moves the cube, configures a Metal
swapchain through Wicked's `Application` and `RenderPath3D`, renders one hidden
frame, verifies the render target, and removes every entity. The probe uses a
one-shot process because this Wicked revision has no public shutdown API for
its global worker systems; it still checks Elisa-side despawn before exit. This
is a real Metal frame smoke test, not yet a complete GDExtension or game loop.

Executed 2026-09-18 on this workstation with defaults
(`WICKED_ROOT`/`WICKED_BUILD` unset, resolving to the sibling checkout and
`build-elisa-arm-o0`): wicked 0.72.114, `GraphicsDevice_Metal` created,
Jolt 5.6.0 / Lua 5.4.8 / FAudio 25.1.0 initialized, `scene
create/update/render passed`, `scene despawn passed`, exit 0. This is the
first in-session executed evidence for the native path; earlier records
described the setup without a fresh run.

## Frame capture (in progress, currently red)

`native/wicked_probe.cpp` now saves its frame to
`build/wicked-frame.png` and `scripts/wicked_probe.elisascript` checks it
with `scripts/compare_renders.py` (stdlib-only PNG parse, dimension scale
check, blank detection, peak-plus-mean tolerance compare mirroring
`src/backend/image_compare.elisa`). The plumbing is verified end to end;
the pixels are not there yet, and the driver fails loudly on that fact.

Ground truths established by probe diagnostics so far (all printed by the
probe on every run):

- Argless `Run()` on a hidden window draws nothing: the window is never
  active, so the driver passes an `alwaysactive` flag through
  `wi::arguments::Parse`, and runs five frames plus `WaitForGPU`.
- `CameraComponent::At` is a facing *direction*, not a target point; the
  default orientation already faces +Z toward the cube, and a 180-degree
  flip was tried and reverted after the frustum dump proved it wrong.
- `TransformCamera` refreshes view matrices only; without an explicit
  `UpdateCamera` the frustum stays stale and culls everything. With it,
  visibility reports 1 object and 1 light, and the cube AABB is exactly
  min=(-1,-1,0) max=(1,1,2).
- `CreateScreenshotWithAlphaBackground` re-renders the *postprocess*
  result, which sits stale when no post effects run; the probe captures
  the swapchain backbuffer via `saveTextureToFile` instead.
- The captured frame is 640x400 (Retina 2x of the 320x200 manifest
  viewport), so the dimension check accepts integer scales.

Ruled out by invariant pixels across runs: lamp existence and intensity
(4 to 30), lamp transform commit, camera orientation both ways, one
versus thirty frames, event pumping, GPU drain, white versus sky-blue
emissive material, MSAA resolve (aliased at 1x), depth target validity
(640x400 present), mesh upload (24 verts, 36 indices, buffers valid),
internal resolution (640x400, so the scissor path is not degenerate),
object render flags and mesh-index linkage (all drawable), close-range
framing (a frame-filling cube changes nothing), and a realistic-sky
control (still black, so the failure is in the shared path, not scene
setup). A Metal System Trace of the probe shows exactly one
probe-owned shader (a compute entry) and no runtime shader compiles.
That alone proves little either way: the checkout ships hundreds of
prebuilt object permutations, so draws could be served silently from
binaries. What constrains the failure instead is elimination across
independent controls: a frame-filling close-range cube, a realistic-sky
background (which needs no objects, lights, or culling), and white
versus emissive materials all produce byte-identical near-black, while
CPU state is verified correct at every level (camera 640x400 physical
with sane near/far/fov, cube world matrix exact, AABB exact,
visibility 1+1 with correct index linkage, valid depth/MSAA targets,
uploaded buffers, sane object flags). A setup defect would move at
least one of those controls; none moves. So the draw is either never
submitted or fully discarded downstream of submission: look next at the
per-instance submit path (render queue, PSO and material bind),
viewport binding values, depth function versus clear value, tonemap
exposure input, light-grid upload, and the HDR compositing branch. Two
methodological notes: `saveTextureToMemory`
must never be pointed at a depth target (Depth32Float_Stencil8 to
buffer trips validation outright), and the probe binary must receive
the inner `WickedEngine/WickedEngine` directory — the outer checkout
root silently starves every shader lookup and segfaults. That is the
next debugging step, not a new policy: the capture, compare, and gate
plumbing is done and waiting for first light.

The manifest the probe consumes (`backends/scene_manifest.txt`) is pinned
to the Elisa canonical scene by `scripts/record_validation.py`, which
rejects drift in version, epoch, entity, camera, viewport, and command
sequence. Positions remain scenario data and are intentionally unpinned.

When the external checkout has been built as `build-elisa-arm-o0`, run:

```sh
WICKED_ROOT="../WickedEngine" \
WICKED_BUILD="../WickedEngine/build-elisa-arm-o0" \
elisascript scripts/wicked_probe.elisascript
```

Set `WICKED_SDL_INCLUDE_DIR` and `WICKED_SDL_LIB_DIR` when SDL2 is installed
outside Homebrew's `/opt/homebrew` prefix.

The current macOS build needs four local compatibility edits in that external
checkout: a `PipelineHash` inequality operator, the SDL2 Apple cursor guard,
and 8-byte alignment attributes for the two FAudio default curves. The probe
also passes `WI_UNORDERED_MAP_TYPE=2` and `WICKED_CMAKE_BUILD` so its ABI agrees
with the CMake libraries. Those edits and flags are deliberately kept outside
this repository; the command above is the reproducible acceptance gate for the
native scene path.

## Toward pixel comparison

`src/backend/image_compare.elisa` already defines the tolerance policy
(per-channel peak plus mean bound), but no backend screenshot reaches it
yet. The probe only checks `GetRenderResult3D().IsValid()`. Wicked offers
`RenderPath3D::CreateScreenshotWithAlphaBackground`, which returns a GPU
texture; turning that into a comparable image still needs a staging
download to CPU pixels plus a file encoder, and no in-repo consumer
demonstrates that chain today. Godot-side capture is likewise unwritten.
Until both captures exist, cross-backend rendering equivalence is probed
at the command-lifecycle level only; pixel comparison stays policy
without evidence.
