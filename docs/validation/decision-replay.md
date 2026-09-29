# Decision log in the replay trace

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is W09 progress and follows
[`decision-log-store.md`](decision-log-store.md).

## Design

`src/runtime/decision_replay.elisa` (`RuntimeDecisionReplay`) connects the
decision log to `Replay`:

- `frame` builds a replay frame whose `random_seed` slot holds the number of
  logged decisions made so far. This is the recorded count, or the replay
  cursor during a replay. A tick that draws more or fewer times therefore
  shows up in an ordinary trace comparison, even when its world digest
  happens to match.
- `first_divergence` returns the earlier of the trace's first divergent tick
  and the log's own divergence tick, or -1 when neither diverged.

## Checks

`test/runtime_decision_replay.elisa` exits 0, and it is wired into
`scripts/check.elisascript`. It covers:

- the decision count stamped per frame;
- a faithful replay with an identical trace, a complete log and a divergence
  of -1;
- a build that draws once more from tick 4, which is reported diverging at
  tick 4 when replayed;
- the same build's own recording showing the extra draw in its frame
  (6 decisions at tick 4).

Negative control: stamping 0 instead of the decision count makes the test
exit 1.

## Gaps

- No shipped runtime system draws through the log yet. The engine's only
  randomness today is the application seed.
- The replay file format still stores frames and the log separately.
