# Counted-fill memory regression — 2026-10-08

## Current result

Frozen compiler `cb10dd72c8e760198f1471636bde26b27d579e95`, product
SHA-256 `2809fb15a2d6e95cebfee520f62ae67aae2c6c45a9ad76bdd81b7d44ccbc4ece`,
cannot yet qualify the engine runtime suite. Both uncached parallel and serial
runs exceeded their RSS watchdogs. An isolated `viewport_mesh` reproduction
identifies repeated exact-size pre-reservation while reading the real 53 MiB
boxer GLB, before mesh parsing. This is separate evidence from the mocap client's
older sealed skin-cache crash; their causes have not been equated.

## Evidence

Retained under `build/validation/`:

- `viewport-mesh-sizes-run.log`: temporary instrumented O0 reproduction writes
  count/capacity after each chunk. Both grow by 65,536 each time. The watchdog
  observes 1,507,008 KiB RSS after about 14 MiB of input and terminates the child.
- `viewport-mesh-no-reserve-build.log` and `viewport-mesh-no-reserve-run.log`:
  compile the unchanged production `test/viewport_mesh.elisa` with only
  `ELISA_DISABLE_MEMORY_RESERVE=1`, using the same frozen compiler/runtime.
  Runtime passes in 0.72s, peak 301,792 KiB, with 80,964 vertices, 90,240 faces,
  zero rest-pose drift and 24,602 inked pixels. The runtime cap remains 524,288 KiB.
- `counted-fill-growth.elisa` and `counted-fill-before-{build,run}.log`:
  a nested counted-fill reproducer compiles at O2, then returns 1 when a later
  capacity increase is smaller than twice the previous capacity. This isolates
  allocation growth without asset parsing or the engine's mesh APIs.
- `counted-fill-growth-evidence.json`: exact commands, terminal statuses,
  compiler/runtime hashes and the explicit unimplemented-fix state.

The failed runtime processes are terminal watchdog terminations. An interrupted
compile also left an empty unrelated test executable; its execution error is
not evidence of a source-level regression.

## Repair scope

`codegen_loop_prereserve.elisa` inserts an exact-size `emit_darray_reserve` before
an inner fill. Repeated outer iterations therefore request one additional chunk
at a time. `arena_realloc` retains moved backing storage because copied array
headers can still refer to it. The interaction produces quadratic retained
storage when blocks must relocate.

Compiler commit `04761c6862cdec8fb8c47d0d1f2f5f47d6f9b618` in isolated
`../Elisa-compiler-counted-fill-fix` routes only
compiler-inserted reserves through the existing geometric darray growth helper.
Explicit user reserve keeps its current behavior. The isolated product
build passes, while compiler main remains unchanged for the running native gate.
Focused qualification passes the expanded nested-fill controls at O0/O2, explicit
reserve/no-shrink and scalar-fill controls, and the existing loop-prereserve
fixture. The original uncached runtime sweep is now running on a frozen product
and matching rebuilt runtime; all 215 tests pass uncached in 25.54s,
peak 1,303,936 KiB under the original 3,145,728 KiB aggregate cap.
Retain existing memory limits and source assertions. Do not install a replacement
product or claim full native/prover compatibility from the ablation result.

## Fixed product evidence

Product SHA-256
`49c58a6128a48f44a85584a4fc3ca78b3fd0849f4ffab1f95f0262854a3b8688`.
Frozen copies are `stage1-code-04761c68` and `runtime-04761c68.o`.

The unchanged real-boxer test passes with the automatic reserve pass enabled:

| Build | Elapsed | Peak RSS | Runtime cap |
| --- | --- | --- | --- |
| O2 | 0.53s | 155,808 KiB | 524,288 KiB |
| O0 | 1.07s | 292,752 KiB | 524,288 KiB |

Both retain 90,240 drawn faces, zero rest-pose drift and 24,602 inked pixels.
`viewport-mesh-sizes-fixed-run.log` retains the exact post-fix count/capacity
sequence: capacity grows geometrically to 67,108,864 for 55,871,924 input bytes.
It completes parsing, skinning and rendering under the same 524,288 KiB cap.
The pre-fix sequence is preserved in `viewport-mesh-sizes-run.log`.
Focused runs use the original runtime to isolate the compiler change; the full
runtime sweep uses the newly built matching runtime. No disable-reserve override
is used in fixed-product qualification.

`compiler-04761c68-runtime.log` and its watchdog JSON retain terminal status 0:
215 total, zero cached compiles, zero remote compiles. This restores runtime
qualification; full prover compatibility and native acceptance of this product
remain open. The concurrent native gate uses the earlier cb10dd72 product.

## Review hardening and integration

Compiler `0baaa951cb7beddcb0ced13c43112203942345bc` resolves the geometric
helper before emitting caller instructions or splitting blocks, and validates
cached helper signatures. Its product SHA-256 is
`30d7507debb10e1b79483268fab6fa509a009bac623e5630ff20f6e2b99e3c16`.
The malformed-helper negative case now produces a safe backend unit decline,
status 2, no object written and no invalid-IR diagnostic. O0/O2 growth controls
and the real boxer test pass with the matching rebuilt runtime; the latter uses
138,992 KiB in 0.49s under the unchanged 524,288 KiB cap. All 215 uncached engine
runtime tests pass in 21.79s, peak 1,326,208 KiB under the original 3 GiB cap.

The hardened O3 bootstrap first exceeded its 8 GiB build watchdog by about
1.75 MiB. After confirming 75% free host memory and terminal native work, a
10 GiB build-only retry passed. Both logs are retained; runtime limits were not
increased. Compiler main fast-forwarded to the two committed fixes. The exact
product and matching runtime were transferred atomically after content provenance
verification; main's normal wrapper passes the growth control. Installed global
wrappers remain unchanged pending broader qualification.

The initial prover build selected the historical frontend pin and was rejected
by borrow exclusivity. The matching-pin build completed but classified the renamed
raw product as Stage0 in its manifest. Neither generation establishes paired
Stage1 provenance. A fresh build through the official Stage1 wrapper, with exact
product/runtime and frontend overrides, is running before engine proof replay.

The official Stage1 build now publishes immutable prover generation
`4a7b62080f134f079518055746cf2a2a`. Both manifests record Stage1, clean compiler
source `0baaa951`, exact product hash above and runtime SHA-256
`ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
The generation passes all 73 engine proofs uncached in 1.47s, peak 173,088 KiB.
`prover-0baaa951-engine-sweep.log` and watchdog JSON retain commands/status;
`prover-0baaa951-engine-reports/` preserves all 73 reports. Full compatibility
matrix, shared gate and replacement-product native qualification remain open.

The matrix's hardcoded compatibility paths now publish official paired generation
`623fb874581a4603acb75e4cc357f417`, with the same verified Stage1 compiler/runtime
identities. The full `scripts/test.sh` is running with `KEEP_GOING=1`, two report
workers and `ELISA_PROOF_SKIP_BUILD=1`. It retains all steps; no baseline or fixture
was removed. Terminal compatibility remains open. The retained 73 engine reports
contain 4,246 obligations, zero unproven/failed obligations and zero replay gaps;
source-inventory limits remain explicit in `prover-0baaa951-engine-inventory.json`.

## Shared gate terminal failure — intentional NaN diagnostics

`compiler-0baaa951-shared.log` finishes status 1 in 41.68s, peak 2,600,960 KiB.
The runtime suite (215 uncached), SDL3/Godot/native probes, owner rejection
controls and cached 73-report proof sweep pass before final report validation
rejects `action-input-context-proof.json`. Both action-input proof reports have
all obligations proved/replayed and zero semantic errors, but four diagnostics
on the intentional `deadzone != deadzone` guard and its postcondition: constant
self-comparison, two float-equality hints and a negated-comparison warning.
The constant-self-comparison message is incorrect for IEEE NaN.

`compiler-0baaa951-shared-diagnostic-gap.json` retains exact diagnostic records
and terminal watchdog metadata. A dependency-free source string analyzed by the
real compiler semantic API reproduces count 4 (`nan-guard-diagnostics-before.log`).
A compiler regression with ordinary integer/float/negated-comparison controls
is prepared in `test/repro/nan_guard_diagnostics.elisa`; its fix remains open.
Preserve the NaN rejection and the shared gate's zero-diagnostic requirement.
Do not suppress report diagnostics or count this run as a shared pass.

A launcher rebuilt from unchanged Script `62928532` with compiler `0baaa951`
passes recovered-helper execution and refuses unrecovered/fallback errors.
`elisascript-0baaa951-build.log` records status 0, 9.32s, peak 1,071,936 KiB;
its broader replacement-product native acceptance remains open.

The two-worker matrix terminated at its aggregate RSS watchdog: 488.03s,
8,599,808 KiB peak against an 8,388,608 KiB cap. Its recorded failure events are
retained in `proof-0baaa951-partial-matrix-summary.json`, explicitly incomplete.
A serial full matrix is running under the same cap; this reduces simultaneous
report workloads while retaining every original check. Neither matrix is a pass.

## NaN diagnostic repair in progress

The compiler now prepares a narrow shared IEEE predicate policy: only direct
f32/f64 parameters in simple guard bodies without declarations, rebinding,
loops or complex scopes qualify. It recognizes same-parameter equality and
inequality as intentional NaN predicates, including their negation. Unknown
contexts retain the original diagnostics; ordinary float equality, integer
self-comparison and unrelated negation are not exempt. Strict float `<`/`>`
self-comparisons remain constant and retain their warning.

The real semantic-API regression passes at O0 (`nan-guard-policy-expanded-run.log`),
including f32/f64 guards, ordinary diagnostic controls, a scoped integer binding,
and runtime NaN/finite/infinity/signed-zero behavior. This executes the changed
semantic source compiled by frozen `0baaa951`; it does not yet qualify a rebuilt
compiler/prover product. The compiler bootstrap is running before focused
proof-report and shared-gate replay. Engine NaN guards and report policy remain
unchanged.

Compiler commit `cfd0203aaea1960f62c8ebdcf6c27677fc6b05dd` lands the narrow
diagnostic repair and the regression. Rebuilt product SHA-256
`9c51a733b8f15603323f02084eaa222d70eb250506af52e513b8dd571e74df3b` passes
`nan-guard-new-product-o2-run.log` status 0. Its clean source fingerprint and
matching runtime are frozen as `stage1-code-cfd0203a` and `runtime-cfd0203a.o`.
A paired prover build runs in a separate checkout of unchanged prover `3e1a6c50`,
so the live serial `0baaa951` matrix keeps its prior frontend snapshot and products.
Fresh uncached runtime qualification is also running; focused action-input report
validation and the shared gate remain open for this replacement product.

### Replacement compiler engine sweep

The official Stage1 paired build completed successfully on unchanged prover
`3e1a6c50`, generation `c2939edb604d45778f8d7f7352bf174c`. Both manifests
authenticate the clean `cfd0203a` compiler product above and the matching runtime;
both immutable binary hashes were checked against their manifests. The uncached
engine sweep passes all 73 reports: 4,246 obligations proved and replayed, zero
unproven/failed obligations, replay gaps, semantic errors or semantic diagnostics.
The action-input context and deadzone reports now satisfy the zero-diagnostic
policy with their production NaN guards unchanged. Source-inventory limitations
remain separate from these obligation results. Retained evidence:
`prover-cfd0203a-engine-sweep.log` and
`prover-cfd0203a-engine-inventory.json`, plus copied reports.

The `cfd0203a` uncached runtime sweep also completed: 215 tests passed in 56.76s
at 1,217,888 KiB peak RSS under the original 3 GiB cap. The replacement shared
gate is running. The prior `0baaa951` serial full compatibility matrix stopped
at its unchanged 8 GiB watchdog limit (8,423,712 KiB peak, 629.10s, status 125);
that is an incomplete matrix with retained failures, not compatibility acceptance.
Its serial log is `proof-0baaa951-serial-full-matrix.log`. Full prover compatibility
and replacement native qualification remain open.

The replacement shared gate completed with status 0: 34.72s, 2,178,192 KiB
peak RSS under its existing 8 GiB cap. It runs all 215 runtime tests uncached,
the shared native/backend probes, and all 73 cached proof reports from the
preceding uncached sweep; the final strict validation report passes. Retained
report: `compiler-cfd0203a-shared-report.json`; log:
`compiler-cfd0203a-shared.log`. This uses compiler `cfd0203a`, paired prover
generation `c2939edb604d45778f8d7f7352bf174c` and Script `62928532` launcher
compiled by `0baaa951`; it does not qualify a replacement full native gate.

Consumer review additionally requests conservative refusal when a source
declaration or alias reuses primitive `f32`/`f64` spelling. A follow-up semantic
policy regression is being qualified separately; the passing product tuple
above remains immutable.

The follow-up shadowing policy now passes the full focused semantic/runtime
regression at O0, including primitive guards, alias `f32 = i64`, user struct
`f64`, ordinary warnings and IEEE runtime controls. Any positive-source-line
symbol named `f32`/`f64` conservatively disables the exemption, including across
modules; unknown resolution retains diagnostics. Retained build/run logs:
`nan-guard-shadow-policy-build-linked.log` and `nan-guard-shadow-policy-run.log`.
This compiles changed semantic source with frozen `cfd0203a`; rebuilding and
qualifying the follow-up compiler product remains required. Earlier failed
invocations reflect the default watchdog cap, a corrected extra reference,
and a missing runtime object from the engine working directory, respectively.

### Compatibility follow-up triage

The serial `0baaa951` matrix retains 72 failure events in
`proof-0baaa951-serial-partial-matrix-summary.json`; no final matrix summary
was produced before the RSS stop. One harness failure uses Python too old for
`dict | None` annotations. Future full runs must place the existing Python 3.14
libexec directory first on PATH, including shell-launched `python3` children.
A focused 3.14 rerun reaches its compiler freshness check rather than that
annotation error; it correctly refuses the old main product while the new seed
is active. This is not a passing witness regression.

The new immutable `cfd0203a` pair independently reproduces the conditional-bound
control failure (`prover-cfd0203a-conditional-control.log`): six cases meet their
expectations, but `rejected_conditional_mutable_input` proves 2/2 with zero replay
gaps. Exact JSON is retained as `prover-cfd0203a-mutable-input-control.json`.
That fixture only marks a parameter mutable; it does not mutate it. Determine
whether the intended refusal is still an admission policy requirement before
changing producer/kernel rules or the fixture. Its acceptance alone does not
demonstrate a false mathematical claim. Preserve the existing matrix expectation
until that review and actual mutation controls justify a change.

Rebuilding `52d60fcf` encountered a live seed already using the same checkout
(PID 42414, verified compiler child 42469). The conflicting invocation safely
refused; it was not a compiler failure. Do not compete with that seed or edit its
inputs. Authenticate its terminal product against the committed source and
freshness manifest before reusing it for qualification.

### Primitive-shadow follow-up product

Compiler `52d60fcf` now has a freshly rebuilt, provenance-checked product, frozen
as `stage1-code-52d60fcf` with matching `runtime-52d60fcf.o`. Product SHA-256:
`8e94a7255bb56d5f0078db60e22c08eb675142d31c57722281bfd69911e551e0`.
The seed retry completed with status 0. Its uncached 215-test runtime sweep
is running under the existing 3 GiB cap (`compiler-52d60fcf-runtime.log`).
A paired prover build with matching frontend revision is running in the separate
qualification checkout (`prover-52d60fcf-pair-build.log`); preserve the older
immutable generations. Neither run is yet terminal qualification evidence.

The previous `cfd0203a` product reproduces all scalar field-copy diagnostic
baselines through both JSON routes (`prover-cfd0203a-scalar-snapshot-diagnostic.log`):
copy across mutation remains 4/5, local-guard and parameter controls pass 5/5,
and stale-field, rebound-copy and wrong-entry cases remain refused. This script
validates the diagnostic counts, not completion of the requested snapshot repair.

The `52d60fcf` runtime sweep completed successfully: all 215 tests uncached,
33.64s, 1,265,152 KiB peak RSS under the unchanged 3 GiB cap. Matching paired
prover generation `d567471a8481487f8f21d3833071c5d7` completed with authenticated
clean Stage1 manifests and checked binary hashes. Its uncached engine sweep
passes all 73 reports in 1.36s at 160,800 KiB peak RSS: 4,246 obligations proved
and replayed, with zero gaps, semantic errors or diagnostics. Retained report
copies and summary: `prover-52d60fcf-engine-reports/` and
`prover-52d60fcf-engine-inventory.json`; full source inventory remains partial.

Script `62928532` rebuilt with frozen `52d60fcf` also passes recovered-error
execution and unrecovered/fallback rejection. `elisascript-52d60fcf-qualification.json`
records hashes and statuses; the build takes 29.85s at 1,844,800 KiB peak RSS.
The shared gate now runs with this matching compiler/prover/launcher tuple.
Full prover compatibility and replacement full native qualification remain open.

The matching `52d60fcf` shared gate completed with status 0 in 42.64s at
1,743,168 KiB peak RSS under the existing 8 GiB cap. The strict final validation
report passes and is retained as `compiler-52d60fcf-shared-report.json`.
Full native qualification is now running (`compiler-52d60fcf-full-native.log`)
with frozen compiler/runtime, matching prover generation, rebuilt Script launcher
and pinned Wicked `fd790f55b3237a9d266335ec742faeacc3cc9228` / SDL3 archives,
under the unchanged 3 GiB native watchdog. No terminal native result is claimed.

### Source checkout isolation for qualification

The first matching native run stopped with status 2 after 173.49s, at the
physics-material smoke. The shared compiler checkout advanced to `b26659e2`
during the run; its wrapper correctly rejected frozen `52d60fcf` against newer
source. The isolated snapshot-transfer prover build also refused this source
drift. These are provenance refusals, not runtime assertions or native acceptance.
Retained native failure report:
`compiler-52d60fcf-native-physics-material-source-drift.json`.

A clean detached compiler checkout, `../Elisa-compiler-52d60fcf-qualification`,
now binds the frozen product to exact `52d60fcf` source. The official provenance
check passes; the matching runtime is copied into its build directory. The
verified product's timestamp was advanced after checkout, with product bytes and
hash unchanged, so the wrapper's separate mtime guard also matches the verified
source. Freshness checks remain enabled. Native qualification retries against
this fixed source root (`compiler-52d60fcf-pinned-full-native.log`). The isolated
snapshot-transfer prover experiment retries against the same root
(`prover-scalar-snapshot-transfer-pinned-build.log`). Both are running.

### Pinned 52d60fcf native terminal result (2026-10-08)

The isolated-source run completed with status 8 after 2,124.95 seconds,
peak RSS 952,640 KiB under the unchanged 3 GiB cap. It failed the
Character Course relaunch stage; subsequent native stages were skipped.
Terminal artifacts are preserved in
`build/validation/compiler-52d60fcf-native-terminal/`; the complete log is
`build/validation/compiler-52d60fcf-pinned-full-native.log`.
The relaunch report records no assertion text. Its device-reopen assertion
returns 264, which becomes process exit 8 on POSIX; this is a candidate cause,
not a confirmed diagnosis. Preserve the real audio acceptance and investigate
with explicit failure diagnostics before another full gate. This run does not
establish full native qualification. The f593c886 prover matrix remains live
and has exposed branch-join and symbolic-quantifier replay gaps.

The focused Character Course smoke/relaunch retry with explicit failure-only
audio diagnostics passes on the pinned compiler/runtime pair (537.79 seconds,
940,112 KiB peak RSS). Log: `build/validation/course-relaunch-diagnostic-cputs.log`;
terminal reports are copied to `build/validation/course-relaunch-diagnostic-terminal/`.
The diagnostic did not fire. This establishes a passing focused retry, not
a reproduced cause or a full native gate pass. Preserve the earlier failed run.

### Explicit-audio native repeat and capture diagnostics (2026-10-08)

The frozen `52d60fcf` full native repeat is terminal failed: 2,341.57s at
984,368 KiB RSS under the original 3 GiB / one-hour watchdog. Character
Course relaunch passes. The async capture smoke exits 39 at its original
positive GPU-duration assertion for the third resized capture. Dependency,
module hygiene and headless stages pass; source length and application fail,
and the final native stage is explicitly skipped. Exact reports are retained
in `build/validation/compiler-52d60fcf-audio-explicit-terminal/`.

The source-length failures were the implementation plan and replay evidence
document. Their content now lives in linked files under the same 600-line
policy; no backlog requirements or evidence records were dropped. Capture
timing now logs raw timestamps and frequency only when unavailable or invalid.
The focused original smoke passes in 44.19s at 839,424 KiB RSS; those
diagnostics did not fire. Its artifacts are retained in
`build/validation/async-capture-timestamp-diagnostic-terminal/`. The full-run
timing failure was not reproduced by that single focused retry; the pass
does not qualify the full native gate. The integrated `ee9c67a9` prover matrix
is still running.


### Resize stress reproduces reversed GPU samples

The same async smoke now repeats its original three-ticket resize sequence
16 times in one process. The original ticket, dimensions, completion, positive
GPU-duration and resource-release assertions remain. On Wicked `fd790f55`,
the stress fails with status 39 in 85.06s at 708,304 KiB RSS under the original
3 GiB cap. Failure-only diagnostics show five reversed sample pairs at
24,000,000 ticks/second, including ticket 28: begin `2778097959261`, end
`2778097959226`. Exact log: `build/validation/async-capture-resize-stress-diagnostic.log`;
retained reports: `build/validation/async-capture-resize-stress-terminal/`.
This is an ordering failure in the GPU samples, not evidence of a frequency
conversion overflow or a missing capture fence.

The Metal 4 SDK documents that both command-buffer and compute timestamps
wait for preceding work but permit subsequent work to start. The old query
path samples the beginning outside an encoder and the end inside the copy
encoder. The first owning-backend experiment records both samples in the compute
encoder with an intrapass barrier. Its archive rebuild and ABI check pass,
but the strengthened regression exits 42 in 258.78s at 650,816 KiB RSS:
tickets 2 and 6 have equal begin/end samples. It does not repair the positive
duration contract. Its source fingerprint and five archive hashes are retained
in `build/validation/metal-timestamp-encoder-products.json`; the log and reports
are `async-capture-encoder-barrier-stress.log` and
`async-capture-encoder-barrier-stress-terminal/`.

The second experiment ends the sample's compute pass with a producer barrier,
so work in later passes waits for the sample and the two samples cannot share
a pass boundary. It also orders pending compute writes before counter resolve's
blit stage, as required by [Apple's counter resolve contract](https://developer.apple.com/documentation/metal/mtl4commandbuffer/resolvecounterheap(_:range:buffer:fencetowait:fencetoupdate:)).
The strengthened regression checks all three ticket durations on every cycle.
The second experiment exits 0 in 267.68s at 593,728 KiB RSS, but is **not
accepted**: all 50 completed timings log an unwritten zero begin sample. The
native admission path previously logged this condition but still returned the
end timestamp as elapsed ticks, producing a false positive based on GPU uptime.
Exact log and retained reports: `async-capture-pass-barrier-stress.log` and
`async-capture-pass-barrier-stress-terminal/`; source/archive fingerprints are
in `metal-timestamp-pass-products.json`.

Native admission now refuses zero begin, equal/reversed pairs and zero frequency
before subtracting or storing a span. Eleven sample cases and three removed-guard
controls pass (`capture-timestamp-pair-controls.log`). The real GPU smoke now
injects zero, equal and reversed pairs only into completed readbacks and demands
TIMING_UNSUPPORTED through the ordinary polling path. The next backend experiment
retains a command-buffer begin sample, makes the following compute pass wait
for preceding queue stages, and orders heap writes before the resolve blit.
The first consumer-barrier compile refuses the test's access to the private
Application.NativeStatus enum; correcting the private test call to compare the
hook's status with zero preserves module privacy. Its actual native retry exits
21 in 44.49s at 852,016 KiB RSS: the initial untampered capture still has a
reversed pair (`2836215221576`, `2836215221458`). The stricter admission now
refuses it. Retained reports: `async-capture-consumer-barrier-stress-terminal/`;
log: `async-capture-consumer-barrier-stress-retry.log`. An independent CPU
resolve of the same completed counter heap confirms the same reversed pair
in both paths: ticket 7 reads begin `2846366445041`, end `2846366444989`.
The diagnostic run exits 39 in 48.96s at 857,808 KiB RSS. This rules out a
GPU-to-buffer resolve difference for that failure and targets sampling order.
Its log is `async-capture-cpu-counter-diagnostic.log`, with reports retained
in `async-capture-cpu-counter-diagnostic-terminal/`. The diagnostic is temporary.
The next experiment samples both non-render boundaries at command-buffer
level after ending active compute work, preserving explicit write/resolve ordering.

The frozen ee9c67a9 prover matrix is terminal failed: 52 failed steps, 2,534.98s,
7,397,232 KiB RSS under the original 8 GiB / one-hour cap. The complete log is
`prover-ee9c67a9-python314-full-matrix.log`, with all 52 failure events retained
in `prover-ee9c67a9-terminal-failure-inventory.json`. The independent 73-report
engine sweep remains narrower evidence and does not qualify the full matrix.

Native qualification is pending. The unchanged implementation-linked pure conversion proof is
`proof/application_capture_timing.elisa` (11/11 certificates independently
replayed in the retained ee9c67a9 engine sweep); that proof does not establish
Metal ordering.


The command-buffer experiment passes in 36.40s at 851,280 KiB RSS under the
original 3 GiB cap: 50 untampered completed captures report positive intervals,
all three injected invalid pairs report TIMING_UNSUPPORTED, queue release and
device-failure recovery assertions pass, and the 880×560 PNG matches the empty
RGBA reference. Only the three deliberately corrupted pairs log invalid timing.
Log and reports: `async-capture-command-buffer-stress.log` and
`async-capture-command-buffer-stress-terminal/`. The temporary CPU resolver is
removed from both repositories. The final source needs a clean repeat before
promotion; full native and prover compatibility remain open, and R17 remains
reopened pending the replacement full gate. No frame-time performance gain is
claimed; timestamp barriers and pass boundaries can affect profiling overhead.


Final source without the diagnostic passes the same 50 positive durations and
three corrupted-pair controls in 35.28s at 851,904 KiB RSS. The final log has
exactly the three deliberate invalid pairs and no missing/invalid real samples.
Engine and Wicked source fingerprints match the pre-run record in
`metal-timestamp-final-products.json`. Retained reports:
`async-capture-final-stress-terminal/`; log: `async-capture-final-stress.log`.
Wicked source repair is committed as `2601ae28beaf8b5e46ebc87507dfd2c1d6f91a82`; the engine manifest
pins that source. The change ends active compute work before sampling its
non-render boundary through the same command-buffer API as the begin sample.
The native admission repair and its tests are bundled with these evidence notes.
This qualifies the focused capture slice, not the replacement full native gate
or full prover matrix. R17 remains reopened until the full gate is established.


The final native-facing unit slot also passes (5.88s, 188,400 KiB RSS),
including the 11 timestamp cases and all three removed-guard controls.
`capture-final-native-unit-tests.log` retains that result. The uncached engine
sweep on verified clean paired generation `17449b782d2f4653aeb28948efa29ab9`
passes in 1.67s at 163,744 KiB RSS: 73 reports / 4,246 original obligations,
all proved and independently replayed, no errors, diagnostics, replay gaps or
trusted assumptions. Exact reports and inventory are retained in
`capture-final-engine-reports/` and `capture-final-engine-inventory.json`.
Its pure timestamp-conversion report retains 11/11 certificates. Full source
inventory remains partial; the 52-step full prover failure is unchanged.

## Replacement full gate and effect lifecycle memory failure

Clean engine `1a472e99` and Wicked `2601ae28` complete the replacement gate
in 1,979.61 seconds / 1,370,272 KiB RSS under the original 3 GiB cap.
Application, dependency, headless, module hygiene and source length stages pass.
Native fails with status 238: renderer group 238 case 16 records 13,776 KiB
of footprint growth against the original 8 MiB allowance. Character Course
relaunch and async capture pass in this run. Full gate remains failed.
Evidence: `compiler-52d60fcf-capture-repair-full-native.log`, watchdog JSON,
and `capture-repair-full-native-terminal/` (including the original executable).

An isolated main calling the unchanged effect lifecycle test passes in 25.24
seconds / 663,856 KiB RSS, with 80 KiB growth. Six subsequent executions of
the preserved full renderer binary also pass, with the original fixture setup
and capture environment restored by the runner. One begins about 13 MiB
higher than the other baselines; this suggests an allocation timing dependency
but does not identify its cause. No tolerance or warm-up change is justified.
The Metal backend defers resource destruction until frame progress; its GPU
wait does not itself drain retirement queues. That source observation alone
does not prove the failing allocation comes from deferred GPU resources.

Evidence: `effect-memory-isolated-baseline.log`,
`effect-memory-full-binary-replay.log`,
`effect-memory-full-binary-repeat-summary.log`, and individual repeat logs.
`effect-memory-reproduction-inventory.json` records all samples and the actual
preserved binary hash. Keep R17 and full compatibility open while tracing this
intermittent failure. Passing replays do not replace the failed full gate.

The diagnostic source now records all-zone live/reserved heap bytes, device GPU
allocated bytes, submission frame count and active renderer pipeline jobs alongside
each original footprint sample. It observes without waiting or advancing frames.
The instrumented native run passes in 91.46 seconds / 1,096,736 KiB; twelve
restored-fixture binary repeats pass in 79.38 seconds / 479,328 KiB. Exact domain
samples: `effect-memory-domains-inventory.json`; source/binary identities:
`effect-memory-domains-source-identity.json`. Logs: `effect-memory-domains-full.log`,
`effect-memory-domains-repeat-summary.log`, and individual repeat logs. GPU bytes
remain stable after the first cycle and pipeline jobs are idle at the sampled
points. Submission counts advance by four per cycle. These passing observations
do not localize the original larger jump or qualify the failed full gate.

A later source build reproduces group 238 case 16 in 64.18 seconds / 1,259,008
KiB: peak growth is 8,208 KiB against the unchanged 8,192 KiB allowance. The
peak falls back before the final sample, while live/reserved heap, GPU bytes
and sampled pipeline jobs do not show corresponding growth. Preserve
`lod-timestamp-admission-native.log` and `lod-timestamp-memory-failure-terminal/`.
This narrows the investigation but does not identify or repair the cause.

The sampler now also records task VM resident/internal/compressed/reusable/device
bytes and available graphics/media/purgeable ledgers from the same footprint
observation. A native renderer build passes in 60.13 seconds / 1,380,336 KiB;
twelve restored-fixture repeats pass in 73.28 seconds / 484,096 KiB. Exact samples:
`effect-memory-vm-domains-inventory.json`; logs: `effect-memory-vm-domains-native.log`,
`effect-memory-vm-domains-repeat-summary.log`, and individual repeat logs.
No changed tolerance, extra settling cycle, renderer leak repair or full gate
qualification is claimed. The related LOD timing admission repair is recorded
in [its focused note](lod-timestamp-admission.md).
