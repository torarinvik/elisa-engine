# Effector drag with an IK hook

`EffectorDrag` (src/animation/effector_drag.elisa) drags an IK effector with the gizmo.

- `begin` takes a gizmo mode, a basis, packed xyz joint positions, a chain listed root-first with the effector last, and the press ray.
  - It checks the chain: 2–8 joints, all inside the skeleton, and no joint repeated back to back.
- `target` gives the effector goal: its start position plus `Gizmo::translate`. Apps with their own IK use this goal directly.
- `solve` is the default hook. It runs FABRIK starting from the pre-drag pose every time, so updates don't drift. The root stays fixed, bone lengths are kept, and joints off the chain are untouched.
- `cancel` returns the pre-drag pose bit for bit, including -0.0 and subnormals.

The index rules are proved in proof/effector_drag_index.elisa (50/50 obligations, all replayed):
- chain length;
- the slot formula `3*joint+axis`, bounded below 768;
- link validity.

test/animation_effector_drag.elisa covers:
- reached, unreachable and invalid outcomes;
- axis and plane targets;
- rejected sessions;
- the exact cancel.

Mutation check:
- All mutants were killed except one: dropping the re-check of FABRIK's Invalid outcome. That mutant is equivalent, because FABRIK leaves the chain untouched when it returns Invalid.
- The fixture keeps the goal well short of full extension. At 98% of the chain's reach, FABRIK needs more than 32 iterations to converge to 1e-6.

Not done yet:
- turning solved positions into local rotations for a skinned pose override;
- a live drag on SDL3/Metal.
