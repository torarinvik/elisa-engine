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

## Gaps

- This is policy only. Cooked skeleton-to-body mappings, Jolt bodies and
  the R08 animation submission are not connected yet.
