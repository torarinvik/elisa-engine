# Effector drag rotations

`EffectorPose` (src/animation/effector_pose.elisa) turns the joint positions solved by `EffectorDrag` into rotations that a skinned pose override can use.

It works on a chain where each joint is the parent of the next.

- `world` computes the new world rotations:
  - Each bone takes its rest world rotation and turns it by the shortest arc from its rest direction to its solved direction.
  - The effector keeps its rest world rotation, so a hand or foot doesn't twist with the limb.
- `local` expresses each world rotation relative to its chain parent. The root's rotation is relative to the unchanged world rotation of its parent outside the chain.
- `arc` handles special cases:
  - Opposite directions: a half turn around a perpendicular axis.
  - Zero-length directions: the identity.

The slot, parent and aim rules are proved in proof/effector_pose_index.elisa: 48/48, all replayed.

test/animation_effector_pose.elisa:
- Drags a three-joint leg whose rest world rotations are not the identity.
- Checks that each bone's own direction, under its new world rotation, points along the solved bone to within 1e-9.
- Checks that the effector's rotation is unchanged bit for bit.
- Checks that the local rotations compose back into the world rotations.
- Checks that an unmoved chain keeps its rotations.
- Checks that bad inputs are rejected.

Mutation check: every mutant was killed except dropping the check that the first direction has zero length. That mutant is equivalent, because a zero direction produces a zero cross product and the formula gives the identity.

Still open: submitting these rotations to a live skinned instance during a gizmo drag.
