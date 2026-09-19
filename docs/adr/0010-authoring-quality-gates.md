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
the AddressSanitizer flavor HANGS before `main` (root-caused 2026-09-18, see
below), and the ElisaScript runner supervising an ASan+UBSan build dies with
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
coverage of the graphics path remains blocked, by a third-party library
initializer rather than by the sandbox; the boundary harness keeps the
ASan+UBSan pair for the unverified libraries.

### What actually blocks the ASan flavor (2026-09-18)

The earlier reading -- "AddressSanitizer aborts before `main`" -- was wrong on
both counts, and it pointed at the sandbox instead of the real cause.

AddressSanitizer starts normally. Under `ASAN_OPTIONS=verbosity=1` the run
reaches `AddressSanitizer Init done` and the probe's worker threads T1-T10 start
and exit cleanly. The nine `Checking file existence is not allowed under
sandbox` lines are ASan's OWN init-time probes for its config files; they are
printed BEFORE `Init done` and ASan continues past them.

The probe does not abort -- it HANGS, and never reaches `main`. A `sample` of
the stuck process puts the main thread in dyld static initializers:

```
dyld4::Loader::runInitializersBottomUp
  -> dllinit            (in libSDL2-2.0.0.dylib, i.e. sdl2-compat)
    -> error_dialog
      -> -[NSAlert runModal]      <- modal alert, blocked in the event loop
```

sdl2-compat is a shim that dlopens SDL3 from its library initializer, and the
only fatal init string in that dylib is "Failed loading SDL3 library."
`DYLD_PRINT_LIBRARIES=1` confirms the split: `build/wicked-native-probe` and
`build/wicked-native-probe-ubsan` both load `libSDL3`, and
`build/wicked-probe-asan` loads it zero times. So under ASan the shim cannot
load SDL3 and raises a modal dialog that nothing can dismiss in a headless run.

A bounded 1800 s run confirms the shape: exit 124 from `timeout`, no sanitizer
report, no crash, no instrumented output.

This is a third-party packaging problem (sdl2-compat's initializer), not an
engine defect and not an ASan defect. Pre-loading SDL3 with
`DYLD_INSERT_LIBRARIES=/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib` was tried and
does NOT clear it -- the probe stops in the same modal alert -- so do not repeat
that. The way out is a build change: link the probe against SDL3 directly, or
against a real SDL2 rather than the compat shim. That is a build-policy call.

**Resolved 2026-09-18 by that build change (a8a27ba).** `scripts/fetch_sdl2.py`
pins and builds real SDL2 2.32.10 under `dependencies/sdl2`, and the probe links
it with `-Wl,-rpath,@executable_path` ahead of the Homebrew prefix (the freetype,
harfbuzz, and zstd includes get their own `WICKED_BREW_*` prefix). The ASan
flavor now reaches `main` and runs the graphics probe. Its first run reports a
real heap-buffer-overflow instead of hanging:

```
ERROR: AddressSanitizer: heap-buffer-overflow ... READ of size 4 ...
  #8 wi::helper::saveTextureToMemoryFile(...) wiHelper.cpp:971
  ... stbi_write_png_to_func ... wi::helper::saveTextureToFile
  #14 main wicked_probe.cpp:546
```

That is Wicked's PNG write callback (`wiHelper.cpp:971`), which pushes every
stb byte through `wi::vector::push_back`; the reallocation path reads one byte
past a 4-byte buffer. A minimal ASan vector-push loop is clean, so the finding
is specific to that callback path rather than a libc++/ASan artifact. The
sanitized graphics probe therefore needs a screenshot path that does not call
`wi::helper::saveTextureToMemoryFile`; recording the upstream defect is the
honest state until then.

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

The minimized case is also recorded in the responsible repository:
`elisa-proof/test/repro/region_branch_facts.elisa` with
`test/repro/check_region_branch_facts.py`.

**Resolved 2026-09-18 in `elisa-proof` 12c79ab.** The cause was not the region
flow losing branch facts. The goal is over a FIELD place (`allocator.last`), and
the interval and difference tiers key a `ProofBound` by a bare identifier, so
only the syntactic negation rule could close it -- and that rule compares
operands with `proof_expr_equal`. The callee contract spells the bound as the
constant `MIN` while the branch facts had already folded it to `0`
(`MIN == 0`, `not(allocator.last < 0)`): equal in value, unequal in spelling.
Operands now match THROUGH a constant pin, mirrored in the kernel replay so a
certificate is still re-derived independently. `check_region_branch_facts.py`
reports PASS (6/6), `proof/entity_id.elisa` is back to 15/15 with zero replay
gaps, and `scripts/check.elisascript` exits 0.

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
