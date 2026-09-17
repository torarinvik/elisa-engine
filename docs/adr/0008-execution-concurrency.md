# ADR 0008: Explicit schedule before concurrency

**Status:** accepted (2026-09-18)

## Context

Effects identify legal concurrency, not profitable concurrency. Parallel
loops over shared mutable world state are a data race with good
intentions.

## Decision

- One explicit serial simulation order first: input, gameplay,
  pre-physics, physics step, commit, post-physics, structural commit,
  render extraction, submission. Fixed 16,667 µs timestep with a
  four-tick catch-up cap; presentation interpolates.
- Systems declare read/write sets over a fixed resource universe; a
  writer orders against every reader/writer of the same resource, two
  readers run together, and orders violating a conflict are rejected as
  data. Parallelism stays flat (phases, never nested pools).
- The 60 Hz frame budget is a target with recorded counters (mean,
  worst, over-budget samples), not a performance claim.

## Evidence

`src/runtime/clock.elisa`, `src/runtime/schedule.elisa`,
`src/runtime/headless_game.elisa`, `src/tooling/inspector.elisa`
(counters), `test/schedule.elisa`, `test/clock.elisa`,
`test/headless_game.elisa`, `test/inspector.elisa`.

## Not covered

Compiler-inferred sets, parallel executor, backend thread-affinity
enforcement, measured workload reports on target hardware.
