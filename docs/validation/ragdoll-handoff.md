# Ragdoll ownership handoff

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is the first P07 progress.

## Design

`src/physics/ragdoll_handoff.elisa` (`PhysicsRagdollHandoff`) is the
policy for which system writes each joint's transform (up to 16 joints):

- Every joint has exactly one owner: Animation, Physics, or Blend. Blend is
  the physics pose easing back into the animation, written by animation.
- `impact(mask)` hands the masked joints to Physics, which is how a partial
  ragdoll works. `recover(mask)` moves Physics joints to Blend.
- `advance` saturates at the recovery time, and completed joints return to
  Animation. `animation_weight` and `blend` give the eased pose in permille.
- A new impact during recovery takes the joint straight back to Physics.
- `despawn` bumps the generation. Old `Ref`s then resolve to NONE, and
  later impacts do nothing.

## Checks

- `test/physics_ragdoll_handoff.elisa` exits 0. It covers:
  - a partial arm ragdoll, with exclusive writers checked on every joint;
  - a full ragdoll and recovery with exact weights (250 at 100/400 ms,
    blend 300);
  - saturation;
  - a re-impact during recovery;
  - recover on non-physics joints;
  - stale and out-of-range references after despawn.
- Negative control: dropping the generation bump and alive check makes it
  exit 12.
- Wired into `scripts/check.elisascript`.

## Skeleton-to-body mapping

`src/physics/ragdoll_mapping.elisa` (`PhysicsRagdollMapping`) cooks a
skeleton's parents and per-joint body indices.

- `cook` rejects:
  - an empty skeleton;
  - a bad or out-of-order parent, or a second root;
  - an unmapped root;
  - a body index out of range;
  - two joints sharing a body.
- `driver` resolves unmapped joints to their nearest mapped ancestor.
  `joints_for` turns a set of hit bodies into the joint mask for `impact`,
  so a hand follows its forearm.
- `test/physics_ragdoll_mapping.elisa` exits 0: it cooks a six-joint arm
  rig, drives the handoff from a forearm hit, and checks six rejections.
  Negative control: dropping the shared-body check makes it exit 6.

## Gaps

- Policy and cook validation only. Jolt bodies and the R08 animation
  submission are not connected yet, and mappings are built in code rather
  than cooked from assets.
