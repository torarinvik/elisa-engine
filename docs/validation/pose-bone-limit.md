# Pose bone limit for mocap skeletons

Mocap rigs carry about 70 bones or more, which the old 64-bone limit on an animation pose rejected. The limit is now 256, and it is set in three places that must agree:

- `RenderSceneAnimationSubmission::MAX_BONES` (src/runtime/render_scene_animation_submission.elisa)
- `ELISA_RENDER_SCENE_MAX_ANIMATION_BONES` (native/render_scene_abi.h)
- `AnimationSubmissionBridge::MAX_BONES` (native/animation_submission_bridge.h)

A pose now takes 256 × 16 × 4 = 16 KiB of bone matrices.

## Evidence

- `scripts/render_scene_native_smoke.py`: rc=0. The out-of-range bone check in test/render_scene_animation_submission_native.elisa reads the limit from the constant, so it now exercises index 256.
- The smoke first failed at group 227, case 129. test/render_scene_snapshot_native.elisa had its own copy of the ABI struct with `f32[64 * 16]`, so the morph data landed at the wrong offsets. That copy now takes its size from the shared constants, so it can't drift again.

## Proof

The pose's slot arithmetic lives in `AnimationPoseIndex` (src/runtime/animation_pose_index.elisa). The module has no floating point, and `set_bone_matrix` and `set_morph_weight` write through it. The submission module takes its limits from `AnimationPoseIndex::BONES` and `MORPHS`.

`proof/animation_pose_index.elisa` proves 34 of 34 obligations, with 34 certificates replayed and 0 gaps:
- Every bone element offset is below 256 × 16.
- A bone written at a legal index is counted, since the new count is greater than the index.
- The bone and morph counts never shrink and never pass their limits.

Four of these goals first left replay gaps. Kernel replay could not refute contradictory arithmetic facts. That was fixed in the prover (see "Replay refutes contradictory arithmetic facts" in ../elisa-engine-proof/AUDIT.md).

## Not yet

- There is no static check that the Elisa constants and the native `ELISA_RENDER_SCENE_MAX_ANIMATION_BONES` agree. For now, drift only shows up as a failing smoke test.
- The live local-transform override, long 120 Hz clip storage with scrubbing, and a typed frame-capacity failure are still open (M02).
