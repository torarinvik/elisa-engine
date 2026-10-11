# Archived evidence: Compiler candidate qualification snapshots

Historical records moved from IMPLEMENTATION_EVIDENCE.md to keep the live release record readable. Preserve their original status and limitations.

## Combined grant/aggregate compiler candidate — 2026-10-09

Compiler source `12120f6b7148ce3f72ea8fba66be29b8cf2825d3` combines default
Global grants, the nested permission-match repair and the large-aggregate
function-value ABI fix. It was freshly seeded from clean Stage0 `735118cb`.
Stage1 SHA256 is
`356d4a14433a90fb14dbf1cfac82e5856eb55205ab9b59da2c7b4eb03a757fbb`, source
tree SHA256 is
`58c14bb3905819a652cc7822274fa77cc615a30c7d3b31eb5c4d2be613f1e57e`, and
matching runtime SHA256 is
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
Stage1 provenance and freshness pass.

This exact product passes the compiler driver's strict semantic check with zero
diagnostics; all 103 mutable-global authority controls; all 24 actual CLI grant
controls, including explicit `-permissive` bypasses; Stage0/Stage1 permission
row parity; the nested permission-match smoke; and the 1,024-byte large-aggregate
O2 smoke. The aggregate smoke's test source required explicit local grants and
now passes in the compiler worktree; that fixture adjustment remains
uncommitted. Gen3 self-host passes stages A–D: five fixed-blocker regressions,
gen2-to-gen3 compilation, byte-identical gen3/gen4 output, and identical output
over 40 reproducibility runs. These checks do not establish the proof pair or
Studio consumer acceptance. The hidden callback-region/result-arena change at compiler
`d5a9b58a` is still being reconciled against newer source; `12120f6b` does not
close the reproduced null-arena FBX crash.

The pinned actual-CLI report is
`build/validation/global-grant-12120-cli.json`. Strict and self-host logs are
under `../Elisa-compiler-global-grant-adoption/build/validation/`.

Using this pair against current engine sources found and fixed two caller/body
distinctions. The C ABI example already declared `Global.Read/Write` in
function signatures but lacked local body grants: 49 strict diagnostics are now
resolved with narrow `can` scopes. Its strict check passes and the native
`scripts/embed_probe.py` host passes archive build, link, session/gameplay
exports and SDL live input. The viewport gizmo test similarly had 12 local grant
diagnostics; scoped callback-counter access/calls now pass strict checking, and
the executable's assertions pass. Logs and executable are under
`build/validation/global-grant-12120-engine/`.

## Full engine gate grant adoption — 2026-10-09

The first exact Stage1 semantic census found missing local `Global.Read/Write`
grants in 27 of the 216 gate entrypoints, concentrated in shared world and audio
APIs and their callers. Local scopes and explicit tail returns were added to 53
affected functions across 34 source/test files. The scopes keep function effect
signatures intact, so callers still need grants. Production scopes are limited
to the specific identity, counter and weighted-variant operations where
possible; test entrypoints scope their stateful API exercises.

On exact compiler source `12120f6b7148ce3f72ea8fba66be29b8cf2825d3`, Stage1
SHA256 `356d4a14433a90fb14dbf1cfac82e5856eb55205ab9b59da2c7b4eb03a757fbb`, and
runtime SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`:

- All 216 manifest entrypoints pass `-emit check`; zero grant diagnostics remain.
- `scripts/run_tests.py <Stage1> -j 2 --no-cache` compiles and runs all 216 tests:
  216 pass, 0 cached, 0 remote, status 0 in 29 seconds.
- The standalone C-ABI embed probe and viewport gizmo assertions continue to
  pass on the same pair.

The report is
`build/validation/global-grant-gate-census-12120-final.json`. The exact runtime
object used for the engine gate was hash-checked against the compiler candidate's
`build/runtime/elisacore_runtime.o` before testing.

This records the 12120f6b pair's acceptance at that checkpoint. The later
4655dbaa Stage1/runtime tuple passed all five strict Character Course entrypoint
checks, the uncached 216-test engine gate and the Maze C-ABI probe. The current
b11e9121 tuple has independently passed those engine gates plus the compiler's
direct Global CLI controls; see the current grant evidence above and
[the latest qualification record](docs/validation/global-mutable-grants.md#current-b11e9121-engine-gate).
The proof pair, full Course build/package, and current Studio acceptance remain
open.

The 600-line source-length gate is complete in engine commit `9e40976a`.
`scripts/asset_cooks.py`, `scripts/package_macos_app.py`, and
`scripts/test_package_macos_app.py` are below the limit after the module/test
split. The checker and focused packaging, build/run and asset-cook tests pass;
full evidence and artifact paths are in
[`docs/validation/source-length-policy.md`](docs/validation/source-length-policy.md).

## Carrier-analysis compiler candidate — 2026-10-09

Compiler source `09f15cf8056825871b83589014596cea5ce70cd4` defers type queries
in generic region analysis. Its focused O0/O2/ASan controls pass 42/42, the
global-permissions smoke passes, and the native backend checks pass 567/567.
The Gen2 driver build completed in under three minutes (the current-main
comparison was about 3m50s). The bounded self-host command
`SELF_HOST_PROBE_TIMEOUT_SECONDS=600 bash test/parity/self_host_gen3_smoke.sh`
passed Stage A 5/5, then Stage B timed out at 600 seconds with exit 124 and no
Gen3 object. The harness reported `baseline 0` and `bootstrap closure BROKE`.
The exact current-main seed reaches the same 600-second limit, so this is a
matched timeout rather than a candidate-only regression. Stage C and fixed
point identity were not reached, so this candidate is not qualified for
promotion. Use a trace/profile to classify the long run; if it is healthy work,
set a justified bound and finish Stage B/C without rebaselining. The matching
normal-CLI performance comparison also remains open; the earlier 66-pass /
12.2-second phase log and 14.9-second full-check median used different routes.

## Incremental compiler baseline — 2026-10-09

The compiler performance owner measured a normal self-host semantic check at
18 minutes 10 seconds of CPU time, but the run rejected the `Str.__cast__`
implementations because their `Unsafe` effects exceed the protocol contract.
That result is not a completed cold-build timing or evidence of a candidate-only
slowdown. The corrected seed has progressed past those protocol errors and is
still building. A later identity-pinned current-main run spent 23 minutes 54
seconds of CPU in mutable-global authority analysis and was stopped with SIGTERM
before the pass completion timer or successful exit. The command used `-emit
check`, so no object was expected. It confirms the semantic cost center but is
an incomplete sample, not a completed cold timing. Revised
controls support the Global-grant rule on the generic `Str` path; the `Atomics`
case remains a warning-level question. The previous phase log's 66 semantic
passes accumulated 12.2 seconds, but its route differs from the earlier
14.9-second full-check median; neither substitutes for the matched valid
baseline.

The compiler project currently flattens an entrypoint and its includes into
one source unit. The incremental design targets persistent reuse across compiler
invocations within that unit: keep per-function body facts separate from
dependency closure, then add generated-body reuse. A function-fact collector
prototype appeared in a separate VAST checkout with in-memory counters, but its
entry point is not wired into the production checker, it has no persistent cache
or cold/warm parity, and its unit fixture is not yet reported passing. Review
found fail-closed risks for local callable shadowing of a top-level function or
`panic`, and partial facts when collection rejects a body midway. Keep it out of
semantic results until those cases, dependency invalidation and clean-build
fallback are qualified. Measure semantic reuse separately from code generation
and relinking; require clean-build diagnostic and artifact parity for no-op,
body-only, and interface/effect edits. There is no measured speedup or
implemented cache to report yet.

## Proof grant migration on compiler `09f15cf8` — 2026-10-09

The proof source migration adds scoped Global grants to 44 functions across
13 modules and is committed as `5a12c8fd` (`Propagate global grants through
proof pipeline`). Initial strict O2 builds against compiler `09f15cf8` exposed
three source issues in two wrappers: `Semantic::check_full_into` now mutates
its `lmut` table and returns `void`, its new final `carrier_surface` argument
was omitted, and the call requires explicit local `Global.Read/Write` grants.
The committed wrappers call it in place with `false`, scope both grants, and
propagate the effects through their callers. The strict O2 build succeeds.

The successful product was built immediately before the source commit, so its
first manifest named the prior proof HEAD and marked the source dirty. The Mocap
Studio freshness gate correctly rejected that artifact. Rebuilding from the
committed tree refreshed the manifest, and a direct freshness check now passes
for proof source, frontend, compiler and runtime. The first optimized Studio
compile hit its default 4 GiB guard at 4.25 GiB, without a source or runtime
diagnostic. The 8 GiB retry passed freshness, file-length and FBX ABI preflights,
then was stopped after ten minutes because repeated samples stayed in the same
LLVM 23 AArch64 SelectionDAG/DAGCombiner path. A two-second macOS sample contained
178 samples, 177 of them in `FoldingSet::NodeEquals` beneath
`SelectionDAG::ReplaceAllUsesWith`; the retry emitted no object or compiler
diagnostic. A matching O1 attempt passed the same preflights but also remained in
the DAG-combine path and was stopped. O0 codegen completed and packaged
`build/MocapStudio.app`; its Studio process is running for inspection. The O0
app main thread is in the AppKit event loop and no new crash report is present;
the original FBX digest is unchanged. Window automation timed out attaching, so
the planned near-60% playback check is unverified. The O0
build reported 23 failed test targets, many with missing Global grant diagnostics.
The post-build proof pass was stopped after multiple laws exceeded 13 minutes.
Its log reports broad Global grant admission failures and missing or unreviewed
proof baselines, so the full corpus is not qualified. The
O0 app uses Stage1 compiler `09f15cf8`, including `806772c7` for the global-rehome
repair. It predates the separate effect-owner index candidate now under parity
review; rerun the grant/proof gate with that indexed product before treating the
diagnostics as source gaps. The failed
O2/O1 attempts remain isolated as failed generations; O0 uses a new
generation. This is not a matched compiler performance comparison and does not
qualify optimized app behavior. The exact
Studio acceptance, all 265 obligations, five CLI regressions and 73 engine
reports remain open. Earlier
diagnostic logs are retained at
`/private/tmp/mocap-cleaner-proof-current/build/compiler-09f-build.log`,
`compiler-09f-proof-rebuild2.log` and `compiler-09f-proof-rebuild3.log`.

## Current default-grant compiler candidate — 2026-10-09

The candidate compiler worktree is clean at source commit
`06e945842698b713ec20a4a9dba9a90ea5823dc9`. Its freshly built Stage1 is
SHA256
`6fcfb4510ad8847d3e3250faaa743d30a3dbb8eb935255ce5373d192f715cc89`; the
matching runtime object is SHA256
`961054709fab91faf2fa62876f1f935a7b0948d586eb5b4dec36c9a3c67917aa`.
The 42-case actual-CLI grant qualification passes on this exact product,
including default strict denials and explicit `-permissive` bypass controls.
The nested global-rehome fixture
`fecfbb561c2df4be342249a17529c6dabba66b3552e2413b0cacd5bd7f6ad96e`
passes at O0, O2 and ASan with local `Global.Read/Write` grants and no
permissive mode. Exact records are in
`build/result-slot-predicate-r2/global-grant-cli-qualification-6fcfb451.json`
and the candidate worktree's
`build/result-slot-predicate-r2/nested-global-rehome-result/manifest.txt`.
The engine strict entrypoint scan passes 89/89 on this exact Stage1 product;
its report is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/global-grant-entrypoints-6fcfb451.json`.
The uncached engine gate also passes on the same Stage1/runtime pair: 216 tests,
0 cached, 0 remote, status 0 in 55 seconds. Its runner independently reports
42/42 CLI controls and 89/89 strict entrypoint checks. The clean command log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/engine-216-current-6fcfb451-clean.log`.

The candidate branch is 14 commits ahead of fetched GitHub `main` at
`62eb050543b1aaa5fdd5a34abc9d2a085e403b9d`, which is its merge base. The
matching main Stage1 product `d8703f781bc05212c2018dcc6c6bde63fa5f5fd767641eedd9c0a0c118b5e1c8`
is fresh for that source; its runtime is
`e6e0d8fe0aee13c176b2c6fd2b111eeda0bd0036b40ac9d3f6fdfdad6d7a0f7e`.
Using the same Stage0 source `ef04267d736eae492c05a023182f6b08f74441a9`
and binary `d2b6da8722ff2b119eeb8bdd9c41e87c4a0d2e08f25dfb1ddac68d6601be38c`,
both candidate and main report the same 28/579 AST parity differences. Thus
these are inherited oracle divergences, not candidate-only regressions. The
exact main comparison log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/emit_ast-main-62eb.log`.

The previous broad fast parity run is not an acceptance result. The compiler
worktree advanced from `ae59c06b` to this source commit while checks were
running, Stage1 was rebuilt during the run, Stage0 products with the same
source commit had different binary hashes, and a signal removed temporary
files during the lowered-emission check. Its green rows and partial failure
counts are retained only as diagnostics. Re-run the required parity on pinned
products and archive their hashes with the results before promotion.

The pinned fast profile remains active. Its selected checks share Stage1
`6fcfb451…`, runtime `96105470…` and Stage0 `d2b6da87…`; assign-target,
annotated-list, C-bind and dependency checks pass. Documentation parity also
passes: 405/733 comparable fixtures are byte-identical against a ratchet of
159, in 2,163 seconds. Its exact result is in
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_doc_parity_smoke.sh.log`.
Format parity passes as well: 397 fixtures are byte-identical against a ratchet
of 173, in 1,359 seconds. Its exact result is in
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_fmt_parity_smoke.sh.log`.
Header parity finishes in 1,068 seconds but fails its baseline: 354 outputs are
byte-identical, 32 diverge, 516 are skipped and 186 are safe Stage1-only
skeletons. The earlier comparison log has the same safe set and two additional
divergences (`sview_closure_after_growth` and
`sview_region_inline_callback_after_destroy`); it lacks matching product
identities, so this does not yet establish a candidate improvement over main.
Rerun the header check on the pinned main Stage1 with this same Stage0. The
candidate result is in
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_header_parity_smoke.sh.log`.
Interface parity also fails 61/333 fixtures, with differences in typestate
summaries, nullable-reference rendering, refinement syntax and Stage1 emission
declines. This matches the earlier unpinned comparison's failure count, but its
exact failed-name set still needs comparison with pinned main. The current
candidate log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_iface_parity_smoke.sh.log`.
Interpret parity finishes in 773 seconds with 100 identical results, three
known Stage0 interpreter bugs, 17 divergent results and 781 skipped fixtures.
The divergences include Stage1 strictness or newer-language behavior; compare
their exact names against pinned main before attribution. Its log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_interpret_parity_smoke.sh.log`.
Lowered-emission parity passes in 1,963 seconds: 358/733 comparable fixtures
are byte-identical, above the ratchet of 146. Its log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/current-fast-gate-6fcfb451/logs/emit_lowered_parity_smoke.sh.log`.
Candidate AST output differs from Stage0 in 28/579 fixtures, matching the
fresh-main result above. Do not count the profile as complete until every
selected check has a result. The current self-host attempt passes Stage A 5/5
but Stage B was terminated with SIGTERM (exit 143) before a Gen3 object; this
is inconclusive, not a compiler failure. Its log is
`/private/tmp/Elisa-compiler-fbx-latest/build/result-slot-predicate-r2/self_host_gen3-62eb.log`.
Retry self-host after the serialized fast profile.
