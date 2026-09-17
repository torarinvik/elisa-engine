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
  found by bisection are recorded: `println` is main-scoped, and
  process-spawning effects do not nest two helpers deep.

## Evidence

`src/tooling/inspector.elisa`, `src/tooling/editor.elisa`,
`scripts/record_validation.py`, `test/check_workflow.py`,
`test/inspector.elisa`, `test/editor.elisa`, `build/validation.json`.

## Not covered

Editing UI surface, play-in-editor, code reload (needs quiescence,
callback draining, migration — explicitly deferred), gate-script split
(required: `scripts/check.elisascript` is at 588/600 lines), license
choice (requires owner decision).
