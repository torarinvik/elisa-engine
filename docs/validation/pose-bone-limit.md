# Pose bone limit for mocap skeletons

Mocap rigs carry about 70 bones or more, which the old 64-bone limit on an animation pose rejected. The limit is now 256, and it is set in three places that must agree:

- `RenderSceneAnimationSubmission::MAX_BONES` (src/runtime/render_scene_animation_submission.elisa)
- `ELISA_RENDER_SCENE_MAX_ANIMATION_BONES` (native/render_scene_abi.h)
- `AnimationSubmissionBridge::MAX_BONES` (native/animation_submission_bridge.h)

A pose now takes 256 × 16 × 4 = 16 KiB of bone matrices.

## Evidence

- `scripts/render_scene_native_smoke.py`: rc=0. The out-of-range bone check in test/render_scene_animation_submission_native.elisa reads the limit from the constant, so it now exercises index 256.
- The smoke first failed at group 227, case 129. test/render_scene_snapshot_native.elisa had its own copy of the ABI struct with `f32[64 * 16]`, so the morph data landed at the wrong offsets. That copy now takes its size from the shared constants, so it can't drift again.

## Not yet

- There is no static check that the Elisa and native constants agree. For now, drift only shows up as a failing smoke test.
- The live local-transform override, long 120 Hz clip storage with scrubbing, and a typed frame-capacity failure are still open (M02).
