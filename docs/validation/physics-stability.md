# Physics stability policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is P08 progress. It is policy only; it is not connected to the Jolt
runtime and no measured stability preset exists.

## Design

`src/physics/stability.elisa` (`PhysicsStability`):

- `combine_friction` is the geometric mean (integer square root) of two
  materials' friction, so a slippery surface keeps a contact slippery;
  `combine_restitution` takes the bouncier one. Inputs clamp to 0..1000.
- `needs_ccd` is true when one step can carry a mover more than half its
  thickness (`speed * step * 2 > thickness`). Invalid or absurd input asks for
  CCD, the safe answer.
- `plan` splits a frame into fixed steps capped at `max_steps` and reports the
  time dropped, so a hitch cannot lengthen the next frame.

## Checks

- `test/physics_stability.elisa` exits 0 (codes 1–12): square roots, friction
  and restitution combination and clamping, a bullet versus a walker, the CCD
  boundary, the step cap and its dropped time, invalid input.
- Negative control: removing the half-thickness factor makes it exit 8.
- Source-length check passes.

## Gaps

- No solver budgets, layer diagnostics or measured presets, and no thin-wall,
  stacking or large-timestep scenario run against the physics runtime, so P08's
  done condition is not met. Full gate still blocked at `world-test`.
