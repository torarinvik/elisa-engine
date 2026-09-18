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

## Allocation benchmark (2026-09-18)

The plan asks for allocations to be benchmarked separately from frame time and
object counts. The native churn probe now records heap bytes in use
(`malloc_zone_statistics`) around its eight 64-cube rounds: the first full round
warms Wicked's pools, and the final reading must stay within 2 MiB of that
steady state. On this machine the steady delta is ~270 KB, so a batch that
leaked would trip the guard even though the component counts return to
baseline. The reading is printed as `churn memory: before_bytes=… steady_bytes=…
after_bytes=… steady_delta_bytes=…`.

## Derived execution waves (2026-09-18)

`Schedule::assign_groups` derives the earliest-fit grouping from the declared
read/write sets: a system joins the first wave holding no conflicting system, so
readers share a wave while a writer is pushed past every reader and writer of
the same resource. `Executor::plan_auto` feeds that grouping through the same
`plan_valid` guard, so an analysis bug appears as a rejected plan rather than a
race. `test/schedule.elisa` pins that the derived plan has two waves, that the
readers share the first wave, and that executing it yields the order
`0, 2, 3, 1` with each system visited once.
