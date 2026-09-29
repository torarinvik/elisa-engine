# Foot placement policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is C05 progress. It is integer policy only; no IK solver, ground probe or
skeleton is connected.

## Design

`src/animation/foot_plant.elisa` (`AnimationFootPlant`), in mm relative to the
character's root plane:

- `pelvis_drop` lowers the pelvis by the lower foot's ground height (never
  raises it), capped at `max_drop`. Out-of-range input or a non-positive cap
  gives no drop.
- `plan` returns the drop and each foot's target height relative to the moved
  pelvis; invalid ground gives zeros.
- `clamp_reach` limits a leg's pelvis-to-foot distance to what a two-bone leg
  can reach, from |upper - lower| to upper + lower.
- `approach` moves an applied offset toward its target by at most `max_step`,
  so a step does not pop.

## Checks

- `test/animation_foot_plant.elisa` exits 0 (codes 1–12).
- Negative control: not capping the drop at `max_drop` makes it exit 3.
- Source-length check passes.

## Gaps

- No two-bone IK solve, no ground raycasts, no foot locking during stance and no
  visual check on a moving character, so C05's done condition is not met. Full
  gate still blocked at `world-test`.
