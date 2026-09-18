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

## Backend lowering fragility found while sampling clips (2026-09-18)

Two compiler/backend behaviours were found while pinning the keyframe sampler
and are recorded because they are easy to hit again:

- Repeated nested struct-array reads can lower incorrectly. Reading
  `clip.keys[1].value.position.x` twice in one condition produced a value that
  disagreed with a single read of the same field. The sampler now stores keys
  as flat parallel columns (ticks, positions, rotations, scales), which keeps
  hot reads flat and reliable; it also matches the plan's preference for
  compact typed stores over records in arrays.
- Include order changes codegen. Sampling returned the last key instead of the
  interpolated midpoint when `state.elisa` was included before `sampler.elisa`,
  and the correct midpoint when the order was reversed. `test/anim_state.elisa`
  includes the sampler first and pins the interpolated value, so a regression
  would be caught rather than silently mis-sampled.

Neither workaround relaxes a safety check; both are recorded as open compiler
issues that the responsible repository should receive as minimized reports.

## Transient compiler crash in the negative fixtures (2026-09-18)

The affine-copy negative fixtures occasionally failed with a nonzero compiler
status and an *empty* diagnostic, then passed on a rerun — a compiler crash, not
a changed diagnostic. `rejects_ownership_copy` now retries **only** the
empty-diagnostic case, once; a changed non-empty diagnostic still fails without
a retry, so a real contract change cannot be masked. The check is bounded and
the fixture still has to be rejected with the expected "linear value"
diagnostic.

## Value blocks snapshot read-only references (2026-09-18)

While adding catalogue validation, a value block over a read-only reference
(`for index in 0..<catalogue.count |ok| -> ok:`) captured a **snapshot** of the
reference and did not observe a mutation made before the call, so corrupted
entries validated as good. Rewriting the loop as a statement loop, which reads
the reference directly, sees the current value. The rule of thumb for this
codebase is therefore stronger than "only mutables may be threaded": a loop that
must observe the caller's current state should read the reference directly in a
statement loop, not capture it in a value block.
