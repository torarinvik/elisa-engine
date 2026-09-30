# Long clips and live pose override (M02)

Date: 2026-09-30. Branch: `mocap-track`.

## What is covered

- **`LongClipIndex`** (`src/animation/long_clip_index.elisa`) holds the limits and the frame-index policy.
  - Limits: MAX_BONES 256, MAX_FRAMES 1,048,576, MAX_SAMPLES 16,777,216.
  - `proof/long_clip_index.elisa` proves that `clamp_frame` and `next_frame` always land inside `[0, count)`, and that an accepted bone count is within its limit. 41 obligations are proven and 0 fail.
- **`LongClip`** (`src/animation/long_clip.elisa`) stores dense f32 local transforms, 10 floats per bone per frame.
  - Supports `create`, `set_local`, `local`, `sample` (lerp plus shortest-arc nlerp, holding the end frames past either end) and `sample_pose`.
  - Typed `ClipError` values: BoneCapacity, FrameCapacity, SampleCapacity and BadRate. They are raised before any allocation.
- **`PoseOverride`** (`src/animation/pose_override.elisa`) evaluates a rig from overrides.
  - The rig takes up to 256 bones, parents first. It raises typed BoneCapacity and InvalidParent errors.
  - For each bone, `evaluate` uses the override if one is set and the clip sample otherwise. `evaluate_rest` works without a clip, using the rest pose instead.
  - `seed_from_clip` hands over from playback with no pop, and `clear_all` hands back.

## Test

`test/animation_long_clip.elisa` (registered in `scripts/check.elisascript`):

```
ELISA_ALLOW_STALE_STAGE1=1 elisac-stage1 -emit exe -o build/animation_long_clip-test test/animation_long_clip.elisa
./build/animation_long_clip-test        # prints the scrub p95; exit 0 = pass
/private/tmp/claude-501/mc/elisa-proof "$PWD/proof/long_clip_index.elisa"
```

- **Storage and sampling:**
  - A 70 × 10,000 clip is created.
  - Interpolation is exact at the half-frame, neighbouring bones are untouched, and times outside the clip hold the end frames.
  - Out-of-range and NaN writes are refused.
  - Each capacity variant is triggered: 0 or 257 bones, 0 or 1,048,577 frames, 256 × 1,048,576 samples, and a zero or NaN rate.
- **Override:**
  - Seeded from playback, the override pose equals the playback pose for every bone.
  - An override on a parent bone moves its child.
  - `clear_all` returns to the playback pose.
  - Rest-pose evaluation works, and shape mismatches are typed.
  - A 256-bone rig is accepted and a 257th bone is rejected.
- **Timing:** 2,000 full-pose (70-bone) samples at pseudo-random times across the 10,000-frame take, timed with `clock_gettime_nsec_np`.
  - p95 = 16–18 µs over three runs on an Apple M5 (reference machine).
  - That is about 0.2% of a 120 Hz frame. The test fails above 2 ms.

## Limits

- The renderer still caps skinned submission at 64 bones (`RenderSceneAnimationSubmission::MAX_BONES`) and `AnimationLimits::MAX_JOINTS`. The pose can be evaluated for rigs up to 256 bones, but it cannot yet be drawn above 64. The boxer rig has 54 nodes, so it fits.
- The API does not include a GLB-to-`LongClip` bake from `GlbDocument` tracks. The app resamples the tracks itself.
