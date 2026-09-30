# Mocap clip storage and scrubbing (M02)

`MocapClip` (src/animation/mocap_clip.elisa) holds a whole take in memory:
7 f32 values per bone per frame (translation, then rotation as x, y, z, w),
laid out frame by frame. A 10,000-frame × 70-bone take at 120 Hz is 4.9 M
values, 19.6 MB. `sample(clip, time_us, bone)` scrubs to any time by
blending the two nearest frames: translation is linear, rotation is the
short-path slerp from `MotionQuat`.

Time is whole microseconds and the rate whole hertz, so the frame lookup
stays integer. Limits: 1,048,576 frames, 256 bones, 1–1000 Hz. A rejected
shape raises `MocapClipError.TooManyFrames`, `TooManyBones` or `BadRate`,
and sizing an already-sized clip raises `AlreadySized`; the clip is left
unchanged in every case.

## Proofs

`MocapClipIndex` (src/animation/mocap_clip_index.elisa) is float-free and
proved in proof/mocap_clip_index.elisa, 83/83 obligations, all replayed:

- the time-to-rate product saturates at 10^14, so it cannot overflow i64;
- any time maps to a frame inside a non-empty clip, and so does the blend
  partner;
- the blend weight is a proper fraction (0 ≤ w < 10^6 millionths);
- accepted shapes stay within the limits, and their storage is non-empty
  and at most 1,879,048,192 values;
- every slot of an accepted clip is in `[0, frames × bones × 7)`.

Getting the replay to 0 gaps meant writing the time saturation as one
`return` guard; clamps written as `a if cond else b` locals and negated
compound guards left replay gaps.

## Tests

test/animation_mocap_clip.elisa (in the gate's asset-test list):

- each typed shape failure, and a refused second sizing;
- fresh storage reads back identity rotations;
- a 10,000 × 70 take reads back exact frames, the first frame before the
  start and the last frame past the end;
- halfway between frames 1200 and 1201 gives x = 1200.5 and a 1.2005 rad
  hinge angle. A mutation of the expected value fails the test (exit 13);
- scrubbing the full 70-bone pose at 2000 pseudo-random times keeps the
  p95 cost under 2 ms.

Measured p95 on the reference Mac (M-series, under load from other
sessions): 16.2 µs and 17.0 µs per 70-bone pose in two runs; one further
run was above 25 µs. The 120 Hz frame budget is 8.3 ms.

Still open for M02: live local-transform override on a skinned instance,
the switch between override and playback without a pop, and render-side
p95 frame time.
