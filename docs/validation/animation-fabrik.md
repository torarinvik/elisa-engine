# Bounded FABRIK chain solver

Validated on 2026-09-29 with the freshly seeded Stage1 compiler. This is C10
progress: the single-chain building block, not yet a full-body rig.

## Design

- `src/animation/fabrik.elisa` (`AnimationFabrik`) solves one chain of up to
  eight joints with FABRIK: a backward pass from the target, then a forward
  pass from the fixed root. Bone lengths are preserved after every pass.
- Work is capped by `max_iterations` (and 64 at most). The `Result` reports
  `Reached` (within tolerance), `Unreachable` (target beyond the chain, so it
  is straightened toward it and the gap reported), `NotConverged` (budget
  spent, with the remaining residual) or `Invalid`.

## Checks

- `test/animation_fabrik.elisa` exits 0 (codes 1–9): a reachable 3D target is
  reached within 1 mm with bone lengths kept to 1e-6 and the root unmoved; an
  unreachable target straightens the chain with a 2 m gap reported; a
  one-iteration budget stops with `NotConverged`; invalid inputs.
- Negative control: skipping the forward pass makes the test exit 3 (the
  root drifts).

## Gaps

- No joint limits, pole targets, multiple effectors, balance or comparison
  against the existing two-bone IK. C10 stays open. No proof harness.
