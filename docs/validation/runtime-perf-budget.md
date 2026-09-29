# Performance budget verdicts

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is Q05 progress. It is the judging policy only; no Tracy zones or trace
capture are wired.

## Design

`src/runtime/perf_budget.elisa` (`RuntimePerfBudget`) judges one `Sample`
(cold, warm and profiled-warm frame times in microseconds) against a
`Budget` (warm limit, cold limit, maximum profiler overhead in permille):

- `overhead_permille` is the profiled run's extra cost over the plain warm
  run; a faster profiled run counts as 0.
- `judge` returns `Invalid` for a bad budget (cold below warm, non-positive,
  or over 1e9 µs) or sample, then `ProfilerTooHeavy` before any budget miss
  so a distorted trace is never blamed on the engine, then `WarmOver`,
  `ColdOver`, `Pass`. A value exactly on budget passes.

## Checks

- `test/runtime_perf_budget.elisa` exits 0 (codes 1–9).
- Negative control: loosening the warm comparison by 1 µs makes it exit 4.
- Source-length check passes.

## Gaps

- No scene- or hardware-specific budget values, no Tracy instrumentation and
  no optimized-build trace. The full gate is still blocked at `world-test`.
