# Character update budgets

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is C09 progress.

## Design

`src/animation/budget.elisa` (`AnimationBudget`) decides how often characters
update, in integers, so the choice is repeatable.

- `stride_for` maps distance to an update stride of 1, 2, 4 or 8 frames using
  near/mid/far tiers; invalid tiers or a negative distance keep full rate.
- `due` staggers updates: a character updates on frames where `frame + id` is
  a multiple of its stride, so consecutive ids spread over the frames.
- `frame_cost` gives the average per-frame cost of a group at a stride
  (rounded up), and `stride_to_fit` picks the smallest of 1, 2, 4 or 8 that
  fits a budget, never below a given floor, else the maximum. `fits` reports
  whether even that maximum stride meets the budget, so an overload is
  visible instead of silent.

## Checks

- `test/animation_budget.elisa` exits 0 (codes 1-16): tier boundaries,
  invalid tiers, staggering, cost rounding, stride selection with and
  without a floor, and a budget that cannot be met.
- Negative control: collapsing the mid tier to full rate fails the test
  (exit 2).
- Wired into `scripts/check.elisascript`.

## Gaps

- No animation runtime consumes these strides, so no character scene ran with
  measured costs; the cost per character is a caller-supplied number, not a
  measurement. Interpolating skipped frames is not handled. C09 stays open.
- No proof written for this module.
