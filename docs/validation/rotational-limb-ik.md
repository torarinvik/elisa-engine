# Rotational limb and floor correction

`Pose::pose_correct_limb` accepts explicit upper/lower/end indices in a
parent-child chain. It derives segment lengths from the current model pose,
solves the knee, updates upper/lower rotations, preserves the end orientation,
and moves every descendant with its nearest corrected ancestor. Unrelated
branches are unchanged. Degenerate and unreachable targets return false before
mutation; invalid shape, hierarchy and indices use existing pose errors.

`Pose::pose_correct_floor_limb` consumes a caller-supplied skinned sole point in
the same model space and a +Y floor height. It corrects negative clearance after
blending, without suppressing intentional positive flight. It does not sample
mesh skin, select contact intervals or convert between world/model spaces.
Those responsibilities remain to be connected to runtime pose submission.

`test/anim_state.elisa` covers non-contiguous indices, non-chain branches, limb
rotation/segment alignment, toes and an auxiliary upper descendant, retained
end orientation, unreachable targets, malformed chains, shape/index errors,
antiparallel directions, floor correction and preserved airborne clearance.
The compiled animation-state executable returns zero with these new tests and
its existing sampler/state/skinning checks. Source length and module hygiene
policies pass; `git diff --check` is clean.

Validation commands:

```sh
/Users/torarinvikbjarko/.elisac/elisac-stage1 -emit exe -o build/anim-state-rotational-test test/anim_state.elisa
./build/anim-state-rotational-test
python3 scripts/check_source_length.py
python3 scripts/check_module_hygiene.py
python3 scripts/elisa_build_run.py build --project . --main test/anim_state.elisa --output build/boxing-animation-ik-test --no-public-runtime
```

The native application-host animation test subsequently built and returned zero
with the compatible Wicked pin. The full boxing application also builds with
Wicked merge `fbcbd38a73cb681fa1b0ed955225d4f9f3bbb33f`, containing upstream
`4323a33c94d021d45404adaf863e9b01673ab365` and Elisa SDL3/Metal/ASTC support.
Its isolated shader preparation produced 398 Metal permutations. Runtime
animation checks are recorded separately; mathematical IK tests do not prove
visual quality or runtime correction integration.

Changes are on `boxing-branch`, synchronized to local main
`e87a5bfa18d2f9eb83ba1dc4c34edaef6ef3ad2c`, which contains fetched remote main
`73516c33f3456a8888833d6d0b2008f513023ecb`. The separate active engine checkout
and unrelated uncommitted work were not modified. Runtime boxer integration,
skinned sole extraction, visual sequence review and
contact/velocity continuity remain open.

## Native rotation timing

`animation_rotation_interpolation.h` replaces normalized linear interpolation
in cooked-joint playback with shortest-arc spherical interpolation. Quaternion
inputs normalize independently; near-identical rotations use a stable linear
limit. The standalone native test checks a 180-degree turn at quarter time,
unequal input norms, opposite quaternion signs, nearly equal rotations, endpoints
and an invalid target fallback. This preserves angular timing for large turns;
it does not implement contact correction or guarantee velocity continuity at
interrupted crossfades. Compile/run with:

```sh
clang++ -std=c++17 native/animation_rotation_interpolation_test.cpp -o build/animation-rotation-test
build/animation-rotation-test
```

## Displayed-pose bridge

`render_scene_animation_readback.elisa` exposes a bounded `AnimationSnapshot`
containing the displayed skeleton locals/parents and Elisa-evaluated model pose.
The native read holds the scene lock and checks thread, handle, hierarchy,
capacity and finite data before copying. It does not sample time, consume root
movement, or include the character's instance placement. The first native test
passed with the cooked boxing fighter, interrupted fades and root movement.

`override_animation_pose` converts corrected model transforms to local TRS in
Elisa and publishes them after full validation. It updates scene joints and the
interruption snapshot source without changing clip time, root movement or morphs.
The next `advance_animation` resamples FK; callers apply corrections each frame
after sampling. Joint names can resolve to stable asset-specific indices during
setup. This is pose plumbing, not automatic foot-contact solving. Native tests
for override, invalid-write rejection, resampling, joint-name lookup and corrected
fade starts pass with the refreshed compiler at `848956f5`. The same test passes
with both dummy and default audio after the isolated Wicked rate fix `fd790f55`. The transform model follows existing Elisa
TRS composition; it is not a general affine shear representation.

The expanded native test also compares every corrected joint's model-space
position with its actual Wicked scene transform (identity instance placement),
within 0.2 mm. It passes on the cooked boxer. This verifies joint transform
submission, not full mesh contact, IK limits or final gameplay visual quality.
Build/run logs reside in the boxing project's `build/pose-stage-scene-check-*`.
