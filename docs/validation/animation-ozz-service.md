# C02 ozz runtime animation service

Date: 2026-10-02

## What changed

- `native/ozz_animation_service.h` promotes the ozz probe into reusable
  services. `OzzRig` holds the skeleton and the engine-to-ozz joint maps, and
  `OzzClip` holds a runtime animation. `OzzPoseContext` and `OzzContextSlot`
  own a sampling context plus SoA scratch. `sample()` writes the engine pose
  layout (10 floats per joint) into caller-owned output and does not allocate.
- `native/elisa_anim_v1.h` is a bounded native reader for the C01
  `elisa-anim-v1` contract. It checks magic, version, length, checksum,
  counts, parent and track order, and key ticks before it builds an ozz rig
  and clips.
- Render scene: skinned instances now sample through ozz.
  - `native/render_scene_ozz_animation.inc` builds one immutable
    `OzzAnimationLibrary` (rig, clips, start and end poses) per cooked asset
    and caches it by asset.
  - Each `InstanceSlot` owns two context slots (current and previous clip)
    and two pose scratch buffers.
  - `apply_animation_pose` samples full poses from that scratch. Crossfades,
    phase matching, interruption, root motion and clones keep their existing
    behaviour.
- The build scripts (`elisa_build_run.py`, `render_scene_native_smoke.py`)
  add the ozz include directory and its three static libraries.

## Evidence

- `test/ozz_animation_service_test.cpp`, run by `scripts/native_unit_tests.py`:
  - `guide_rig.anim` matches a linear/nlerp reference. The worst error is
    3.8e-4; ozz stores translation and scale keys as half floats.
  - Four tampered images are refused: a bad checksum, a forward parent, a
    truncated file and an out-of-order track.
  - 256 characters × 300 ticks: 192 guide rigs plus 64 synthetic 64-joint
    rigs with 3 clips, each with its own clip and time. The 76,800 samples
    made 0 calls to `operator new` and 0 calls to the ozz allocator.
  - Independence and determinism hold. A control confirms that context
    preparation is counted.
- `test/render_scene_animation_crowd_native.elisa`, render group 271 cases 71–77:
  - Eight skinned instances of two cooked assets play `lift`, `drop` and
    `root-motion` at different speeds with 0.25 s fades.
  - They run 119 steady ticks with varying per-instance deltas through the
    public `RenderScene::advance_animation`.
  - The test-probe allocation counter (armed inside
    `elisa_render_scene_v1_advance_animation`) is unchanged across those ticks,
    and the phases stay independent.
  - Mutant check: one heap allocation added to `sample_ozz_full_pose` makes
    case 75 fail.
- The existing animation native tests (submission, blend, root motion, clone)
  and the SDL3/Metal render smoke pass on ozz sampling without widening any
  tolerance.

## Gaps

- The runtime still feeds ozz from the `.pkg` fixed-rate tracks. The
  `elisa-anim-v1` reader is tested natively but is not yet the runtime's clip
  source.
- The first tick after binding a clip set prepares contexts and allocates.
  Only steady-state ticks are allocation-free.
- No new Elisa source logic was added, so there is no new proof. The new
  Elisa file is a test.
- The ozz service test is not yet in `run_boundary_sanitized.py`.
