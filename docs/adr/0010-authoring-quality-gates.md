# ADR 0010: Authoring support and quality gates

**Status:** accepted (2026-09-18)

## Context

Content iteration without validation drifts; validation without
provenance cannot be trusted; a green gate that does not cover a
subsystem proves nothing about it.

## Decision

- Runtime inspector snapshots read authoritative state without mutating
  it; content edits go through bounded undo/redo (branching truncates
  redo) and asset reloads bump explicit generations.
- `build/validation.json` records source hashes, tool identities, proof
  replay outcomes, and the check list; stale reports are deleted before
  any run and written atomically only on full success.
- The shell↔ElisaScript parity harness (15 fake-tool cases) must match
  exactly. Its bounded retry on exit 126 covers only launcher-side
  spawn failures no fake tool emits; genuine divergences still fail.
- Toolchains move as snapshots with recorded revisions: stage1
  a950b5cd could not emit `catch` over `void`, so the snapshot moved to
  76230afa with the minimized repro documented. Script-authoring limits
  found by bisection are recorded: `println` is main-scoped, process
  effects do not nest two helpers deep, and `main` must end with a value
  tail (ending on a `_ =` statement reports `"main" must return a value`).
  A `run_suite` dedup helper and a driver-plus-parts split were both
  attempted and reverted: byte-identical suite blocks verify inside the
  full 588-line gate and fail bare `VerificationFailed` in short files,
  a whole-file context sensitivity whose root cause is not isolated
  after ~20 probes. The gate therefore stays monolithic until the
  verifier explains itself; no further suites land before the split.
- Further script-authoring limits found while adding host drivers: an
  f-string whose interpolation is itself a call, as in
  `f"@{str(path)}"`, aborts the launcher with exit 133 and no output
  (a literal or a plain variable works, so the trigger is the nested
  call, not the process-helper count). Host drivers therefore build
  argument strings with plain bindings, and each driver verifies a frame
  with one `compare_renders.py verify` call covering dimensions,
  determinism, and topology instead of stacking several checker calls.
- A helper whose body calls filesystem or environment host functions can
  fail verification when the caller does not feed its result straight into
  a process call; `scripts/maze_game.elisascript` sidesteps this by naming
  the compiler (the launcher resolves an executable name through PATH)
  instead of scanning for it. The packaging driver is separate from
  `scripts/check.elisascript` because that script is at its 600-line limit.

## Evidence

`src/tooling/inspector.elisa`, `src/tooling/editor.elisa`,
`scripts/record_validation.py`, `test/check_workflow.py`,
`test/inspector.elisa`, `test/editor.elisa`, `build/validation.json`.

## Not covered

Editing UI surface, play-in-editor, code reload (needs quiescence,
callback draining, migration — explicitly deferred), gate-script split
(required: `scripts/check.elisascript` is at 588/600 lines), license
choice (requires owner decision).
