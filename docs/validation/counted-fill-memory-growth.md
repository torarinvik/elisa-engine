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
