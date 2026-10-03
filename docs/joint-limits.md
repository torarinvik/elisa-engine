# Authored joint rotation constraints

`JointLimits` projects local rotations into an authored circular swing cone and
signed twist interval. Angles are radians. `SwingTwist.reference` defines the
neutral rotation in the joint parent's frame; `axis` is expressed in that
reference frame. Axes are normalized. Zero maximum swing gives a hinge; a
maximum swing of pi with twist [-pi, pi] is unrestricted.

`project` reports source and projected swing/twist angles and whether a limit
was applied. Valid unconstrained inputs are retained exactly. Invalid rotations,
axes or intervals raise before producing a result. Quaternion double-cover
inputs have the same projected orientation. Signed twist has a branch cut at
+/-pi. At a 180-degree swing, twist is underdetermined; the implementation uses
a deterministic identity-twist convention. Temporal tracking or anatomical
calibration cannot be inferred from this convention.

`PoseJointLimits.project` applies an indexed constraint set to a model-space
pose after IK/FK weighting and before native pose override. Local offsets and
scales are extracted from the current pose. Changed rotations move their
connected descendants; untouched branches preserve exact model transforms.
All input validation and computation finish before the caller's pose is changed.
Disabled slots need no valid limit. Working arrays are bounded by MAX_JOINTS.

Projection is not a constrained target IK solve. It can move the end effector
away from its target and change its orientation. Sports/contact policy must
coordinate reach, pelvis, end orientation and authored limits rather than
repeatedly alternating incompatible projections. Elliptical shoulder cones,
soft limits, temporal singularity handling and coupled constrained IK remain
future work. Bone-axis/reference calibration belongs to rig setup; these values
must not be guessed from engine/world axes or reused blindly between skeletons.

Tests: `test/joint_limits_main.elisa` covers cone/twist ranges, reference frames,
antipodal inputs, singular swing, invalid inputs and a 601-angle hinge sweep.
`test/pose_joint_limits_main.elisa` covers connected descendants, unrelated
branches, local lengths, disabled slots and failed-write atomicity.
`test/render_scene_joint_limits_main.elisa` exercises synthetic constraints on
both fighter rigs through the native pose bridge. Those synthetic constraints
verify the API and do not establish anatomical suitability.
