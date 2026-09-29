# Bounded diagnostics log and telemetry policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is Q04 progress.

## Design

`src/runtime/diagnostics.elisa` (`RuntimeDiagnostics`) is a fixed ring of
eight numeric records: sequence, level, subsystem, code and value.

- There is no free text, so a project path or user content cannot enter a
  record by accident.
- `append` drops events below the level filter and counts them as filtered.
  When the ring is full the oldest record is overwritten and counted in
  `dropped`, so an artifact is always small and says how much was lost.
- `recent(age)` reads newest first; a missing age gives sequence 0.
- `Telemetry` is off by default (`allows` is false for everything). Crash and
  metrics classes need `configured`; project-path and user-content classes
  additionally need `private_opt_in`, and an opt-in without configuration
  sends nothing.

## Checks

- `test/runtime_diagnostics.elisa` exits 0 (codes 1-15): filtering, ordering,
  wrap-around and drop counts, out-of-range ages, and every telemetry class
  with and without configuration and opt-in.
- Negative control: making the private classes always allowed fails the test
  with code 13.
- Wired into `scripts/check.elisascript`.

## Gaps

- Nothing writes the ring to a crash artifact, and no launcher records it;
  symbolication, reproducible launch arguments and Sentry Native are not
  started. Q04 stays open.
- The ring size is eight, chosen for tests; a shipping size is not measured.
- No proof written for this module.
