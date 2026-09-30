# Pop-free pose source switching (M02)

`PoseFade` (src/animation/pose_fade.elisa) lets a skinned instance switch
between pose sources without a pop. The sources are the app's live
override, a scrubbed `MocapClip`, or cooked playback. At the switch the pose
on screen is held and blended towards the new source over a short window:
translation linearly, rotation by short-path slerp. `transform_of` turns a
sample into the `Geometry::Transform` that `RenderScene::set_bone_transform`
submits. The native bridge applies each submitted matrix as that bone's
local transform, so a fed clip or override needs no extra native path.

## Proofs

`PoseFadeIndex` (src/animation/pose_fade_index.elisa) is integer-only and
proved in proof/pose_fade_index.elisa, all obligations replayed:

- progress stays within [0, 10^6] millionths for any clock values;
- a zero-length fade is complete at once;
- a fade whose elapsed time reaches its duration is complete;
- the fade clock never runs backwards.

Two of these needed prover fixes, landed in ../elisa-engine-proof. A
callee's `result == c or d > 0` could not be used with a literal `0`
argument, and `requires e >= d` could not rule out a disjunct `e < d`. See
that repo's AUDIT entry "Closed false and complementary premises close a
case split".

## Tests

test/animation_pose_fade.elisa (in the gate's asset-test list):

- the held pose shows unchanged at the switch, which is the no-pop
  property;
- halfway through, the blend is the midpoint in translation and the
  half-angle in rotation;
- the weight never falls over 30 steps and ends on the new source;
- long fades are capped at 10 s;
- a sample survives the round trip through `Geometry::Transform`.

A mutated midpoint expectation fails the test (exit 4).

Still open for M02: wiring the fade into a render-scene instance and the
render-side p95 frame time.
