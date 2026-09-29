# AI decision model

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is N04 progress.

## Design

- `src/ai/brain.elisa` (`AiBrain`) is a deterministic, integer-and-float
  decision model with four modes: Patrol, Chase, Search and Interact.
  - `perceive` combines a squared-distance sight cone with a hearing test
    (range and loudness), so no square roots are taken.
  - `think` takes the brain, a percept, the tuning and whether the agent has
    arrived, and returns a `Decision` with the next brain, whether to move
    and the goal. It remembers the last seen position, gives up a search
    after `search_ticks`, and holds an interaction for `interact_ticks`.
  - Every mode change folds (mode, tick) into an FNV-style digest, so two
    runs of the same percept script can be compared by one number.
- `src/ai/patrol.elisa` (`AiPatrol`) holds the integer route arithmetic.
  `next_waypoint` wraps after the last point, and `guarded_next` restarts
  the route when a stored index no longer names a waypoint or the route is
  empty.

## Proof

`proof/ai_brain.elisa` proves that a patrol advance stays on the route
(`result < points or points == 0`) for any index, route length and
arrival flag. It proves with 11/11 replayed. The helper lives in its own
integer-only module because the float-typed decision model is unsupported by
the prover. `next_waypoint` uses an explicit wrap, not `% points`, which the
prover does not yet bound.

## Checks

- `test/ai_brain.elisa` exits 0 (codes 1–21). It covers a scripted target
  replay through patrol, chase, interact, search and give-up, the replay
  digest, and perception cases: behind, loud noise, occluded, quiet, wide
  angle, far, heard-only chase and waypoint cycling.
- Negative control: disabling the search give-up makes the test exit 10.
- The test is compiled and run by `scripts/check.elisascript`.

## Gaps

- The brain is not yet driving an agent in the character playground: no
  moving body, NavAgent path or rendering uses it.
- The prover does not yet bound `x % n < n`.
