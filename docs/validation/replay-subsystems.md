# Replay subsystem digests

Validated on 2026-09-29 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is W09 progress; it complements the
existing whole-frame `Replay.first_divergent_tick`.

## Design

- `src/runtime/replay_subsystems.elisa` (`ReplaySubsystems`) records one
  digest per subsystem per tick (input, world, physics, ai, animation, audio;
  up to 16 ticks) and `first_divergence` names the first divergent tick and
  the lowest-index subsystem that differs at it.
- Each subsystem declares a determinism scope. Input, world and ai are
  `Exact`. Physics and animation are `SameBuild` (solver and floating-point
  results are compared only between runs of one build and platform). Audio is
  `Excluded`. Cross-platform bit identity is not promised for what cannot
  keep it.
- `judge` gives `Match`, `Diverged` or `BadShape` (different tick counts).

## Checks

- `test/runtime_replay_subsystems.elisa` exits 0 (codes 1–11): identical
  traces, same-build vs cross-platform physics divergence, earliest-tick and
  lowest-subsystem ordering, excluded audio, length mismatch, range and
  capacity refusal.
- Negative control: declaring audio `Exact` makes the test exit 8.

## Gaps

- Not wired to `Replay` recorders or the runtime's real subsystem digests;
  the 16-tick window is fixed. Recording decisions and seeds is not covered.
  W09 stays open. No proof harness.
