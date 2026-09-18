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

## UBSan on the full graphics probe (2026-09-18)

The boundary harness has run under AddressSanitizer and UBSan from the start;
the full graphics probe could not. Two separate blockers, now separated:
AddressSanitizer itself aborts before `main` inside this session's sandbox
("Checking file existence is not allowed under sandbox", no instrumented
output), and the ElisaScript runner supervising an ASan+UBSan build dies with
`runtime: Time` in its child-wait sleep. A UBSan-only flavor therefore has its
own flag, `ELISA_SANITIZER=undefined`, and the full graphics probe runs under
it with the standard runner:

```
ELISA_SANITIZER=undefined CXX="$PWD/scripts/cxx_sanitize.py" \
  elisascript scripts/wicked_probe.elisascript
```

The first run found real undefined behavior: `native/package_load.h` decoded
base64 into a signed `int` accumulator whose fourth sextet shifted into the
sign bit (`runtime error: left shift of negative value`). The accumulator is
now `uint32_t`, and the probe exits 0 with no sanitizer report. AddressSanitizer
coverage of the graphics path remains blocked by the sandbox; the boundary
harness keeps the ASan+UBSan pair for the unverified libraries.

## Debug collision geometry (2026-09-18)

The inspector's list includes debug collision geometry. `DebugGeometry` owns
cell-box construction (cell extent 0.6, matching the hosts' wall placement) and
a bounded collection that refuses an overflow instead of dropping geometry;
`examples/maze/debug.elisa` builds the boxes from the same rule tables
collision uses, so solid cells, door, hazards, and goal cannot disagree with
gameplay. `test/inspector.elisa` pins the box math, kind counts, and the
capacity guard; `test/maze_game.elisa` pins the maze counts against
`maze_wall_count`; the Godot capture builds a wireframe `ImmediateMesh` for
every visible wall (9 visible, 216 line vertices, 12 edges per box) and frees
it before capture, so the frame comparison still passes.

## Prover regression: branch facts from a mutable reference field (2026-09-18)

The prover rebuilt from `elisa-proof` e2fadd8 ("Align proof contracts with
fresh compiler safety checks") no longer establishes a callee precondition
when the argument is a local bound from a **mutable reference field** through
branch guards. The gate is blocked on this: `proof/entity_id.elisa` regressed
from 15/15 to 13/15 (`call-requires-unproven [unknown]` at
`src/entity_id.elisa` line 31), with the same engine source and compiler.

Minimized repro (28 lines; 4/6 proven with the regression):

```elisa
module Guard:
    const MIN: i64 = 0
    const MAX: i64 = 9223372036854775807

    error TooBig:
        Low
        High

    affine struct Allocator:
        last: mutable i64

    def next(last: i64) -> i64:
        requires last >= MIN
        requires last < MAX
        ensure result > last
        ensure result == last + 1
        last + 1

    def allocate(allocator: mutable Allocator&) -> i64 error[TooBig]:
        cursor: i64 = allocator.last
        if cursor < MIN:
            raise TooBig.Low
        elif cursor >= MAX:
            raise TooBig.High
        else:
            candidate: i64 = next(cursor)
            allocator.last <- candidate
            candidate
```

Observed bisection: replacing `cursor: i64 = allocator.last` with a plain
`cursor: i64` parameter proves 6/6; removing the `allocator.last <- candidate`
write does not help; early-raise style instead of `elif/else` proves 5/6. The
engine source was not restructured to hide this, per the plan's rule against
moving contracts to make an integration appear to pass.

## Evidence

`src/tooling/inspector.elisa`, `src/tooling/editor.elisa`,
`scripts/record_validation.py`, `scripts/cxx_sanitize.py`,
`test/check_workflow.py`, `test/inspector.elisa`, `test/editor.elisa`,
`build/validation.json`.

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
