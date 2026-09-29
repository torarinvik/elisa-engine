# Deterministic crowd avoidance

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is N05 progress.

## Design

`src/nav/crowd.elisa` (`NavCrowd`) computes separation pushes for up to eight
agents in whole units.

- `pair_push` pushes agent `i` away from `j` when they are closer than the
  personal radius, in proportion to the overlap along the line between them.
  Two agents therefore get equal and opposite pushes. Coincident agents split
  along +x for the lower index and -x for the higher, so a stack separates.
  The same agent, invalid indices, out-of-range positions or a zero radius
  give no push.
- `avoid` sums the pushes from every other agent and caps each axis, so
  avoidance cannot overpower path following. A zero cap gives no push.
- Everything is integer (with an integer square root), so a run repeats
  exactly.

## Checks

- `test/nav_crowd.elisa` exits 0 (codes 1-10): opposite pushes, diagonal
  direction, radius edge, stacked agents, cap, a balanced middle agent and
  invalid input.
- Negative control: pushing coincident agents the same way fails the test
  (exit 5).
- Wired into `scripts/check.elisascript`.

## Gaps

- Separation only: no velocity obstacles, reciprocal avoidance, lanes or
  formations, and it is not combined with Detour path following. The plan asks
  for crowds beyond eight agents and a measured budget with W07 parallelism;
  neither exists. N05 stays open.
- No proof written for this module.
