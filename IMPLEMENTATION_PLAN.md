# Elisa Engine — native implementation backlog

**Updated:** 2026-10-09. **Focus:** close measured release blockers, then ship reusable public-API games on Wicked + SDL3.
**Baseline:** inspect the current tree and linked validation notes before starting work.
This is the active execution plan. [Architecture](Elisa_Engine_Architecture_and_Plan.md) defines the ownership model; [capabilities](docs/capabilities.md) records evidence.
Unfinished tasks below are proposals, not claims of existing engine support.

## Product objective and priorities

Build a reusable, authorable, shippable native game engine beyond the maze demo.
Deliver a persistent playable host, reusable subsystem services, a practical editor, and packaged games using the same Elisa-owned world and public API.

- **P0:** remove foundations that currently prevent a reusable native engine.
- **P1:** deliver the common features needed to build and ship a substantial 3D game.
- **P2:** add depth, scale, multiplayer, production tooling, and broader platforms.
- **P3:** specialist capabilities, with a real example and a measured need.
- Near-term work prioritizes making the existing Wicked, SDL3, Jolt, miniaudio,
  cgltf, BasisU, meshoptimizer, ozz, Recast/Detour, and GameNetworkingSockets
  integrations into usable engine services. Prefer finishing these vertical slices
  over adding another dependency or a probe-only wrapper.
- **Active workstream:** finish the reusable native game runtime first. Do not move
  editor-shell, multiplayer, alternate-platform, or specialist-library breadth ahead
  of the backend sequence below unless a prerequisite or test failure requires it.
- A library integration is complete only when an ordinary Elisa application can use
  its public service, resource ownership and failure paths are tested, and a real
  sample consumes it. A native probe is supporting evidence, not the end product.
- Godot receives compatibility maintenance for changed shared contracts. New features
  may initially be native-only; report unsupported capabilities explicitly. Godot
  installation or renderer feature parity must not block the native development gate.
- Reuse the selected stack. Do not introduce another renderer, foreign gameplay ECS,
  high-level UI framework, or competing public scheduler to avoid engine work.
- Specialist integrations remain a backlog until a concrete game requirement or measured
  bottleneck justifies them. Finish usable paths through the selected stack first.

## What exists, and what must not be mistaken for completion

The checked world, private owner fields, expression style, affine rejection fixtures, maze gameplay, scene recorder, asset cooking, and implementation-linked proofs exist. Preserve them. The current runtime executor is a **serial reference**, not a thread pool.
The ordinary Elisa Application/RenderScene path, public Jolt physics services (P01–P06),
miniaudio playback and world attachment, rendered animation, navigation queries, and
relocated macOS packaging already have implementation evidence below. Extend these paths;
do not rebuild them from their original probe descriptions. Remaining acceptance varies
by service: unchecked tasks often contain substantial working implementations.
A linked library, a policy enum, or one successful scene is not a public runtime service. The existing source inventory is the starting point, not a reason to rewrite working code.

## Agent execution contract

1. Follow the ordered active queue below, then task prerequisites; subsystem P labels describe eventual importance, not permission to bypass the queue. Work through one coherent playable or testable slice; do not scaffold every wrapper before using one.
2. Read the relevant existing modules, ADRs, local library headers, and pinned source. Verify APIs and actual platform support there; this plan does not certify upstream features.
3. Elisa owns gameplay, world identity, scheduling policy, asset orchestration, animation behavior, navigation decisions, replication, editor models, and the public API. Keep C/C++/Objective-C shims narrow: opaque handles, plain ABI data, vendor operations.
4. Follow the current compiler [STYLE_GUIDE.md](../Elisa-compiler/STYLE_GUIDE.md) in new and touched code. Use its working expression forms: block initializers, loop results and header captures, owned value-threading and builtin container value forms. Keep temporary names within their useful scope; preserve zero-iteration values, evaluation order, borrow lifetimes and escaping allocations. Named `region` scopes end allocation lifetime too, so keep backing buffers alive for borrowed views. Prefer `def Type() -> Type` constructors, named constants and appropriate standard-library facilities. Apply style during a functional slice; a compiler feature or lint finding alone does not justify a repository-wide rewrite. The current qualification baseline is below; older compiler runs are historical evidence.
5. Use qualified dependencies, small public surfaces, private owner fields, and phase-limited, disjoint call borrows. The current compiler checks overlapping borrows through reference locals, returned/conditional references, containers and function values; audit those paths when touching a call. Split borrows across disjoint fields or pass scalars by value; assign results where global mutation conflicts with a borrow. Unsafe code is opaque to this check and needs explicit alias review. On qualified Stage1 products, prefer owned value-threading (`x <- f(x)`) and builtin container value forms in touched code; return the same owning type (or a two-result tuple), omit `mutable` on the threaded parameter, and put owned fields back in the same statement. Keep reference APIs for borrowed owners, arenas and side-effect-only calls. Two-result builtin updates must target the original container first; audit every return, expression-position caller and function-value use before converting a helper, because those uses retain by-value semantics. Scalar input/result helpers are ordinary value functions; views are not threaded owners. These forms have compiler parity evidence, not an engine performance gain; see [targeted adoption rules](docs/plans/runtime-next-slices.md#compiler-style-guidance-applied-to-the-active-queue). Use `-Wnever-leak=strict` to triage touched code: gentle mode reports only rewritable accumulators; strict also explains candidates blocked by loop reads, later mutation, non-scalar storage or outer jumps. For touched accumulator sites, prefer supported scalar/optional-scalar loop results, tuple yields for multiple results and optional search results initialized to null; preserve zero-iteration values, evaluation order and borrow lifetimes. Treat strict findings without a valid rewrite as diagnostic guidance, not an instruction to force a conversion. Enable `-Werror=never-leak=strict` per directory once clean; avoid style-only sweeps. Keep vendor types out of public Elisa interfaces and files below 600 lines; split by responsibility.
   Current guide addition (`STYLE_GUIDE.md` change `dd8aea22`, compiler `b11e9121`): mutable-global reads require `Global.Read`, writes require `Global.Write`, and read-modify-write requires both by default. Declare required effects on functions and use local `can` blocks to scope access; callers also carry the capability. Keep `-permissive` for explicit diagnostic or migration controls, never as normal acceptance. Earlier guide guidance (`e3087e23`, read at compiler `b26659e2`) still applies: after qualifying the selected product, prefer comprehensions for touched pure collection builders and labelled loop results for nested searches. Ordinary block initializers may contain loop exits; exits from capture blocks remain forbidden. Triage `push loop` and `value-thread` lint findings; preserve ordering, allocation lifetime and proof replay. Prioritize builders encountered during package refresh and ordinary-project rehearsal; see the targeted adoption rules above.
6. Preserve real error unions and failure atomicity. Define resource/thread/allocator ownership, callback lifetime, cancellation, capacity behavior, and destruction order.
7. A compiler, ElisaScript, or prover limitation blocking this style becomes a minimized regression and a fix in its owning repository. Do not flatten scopes or weaken checks as the permanent workaround; record the required toolchain revision. Prover holes, meaning engine constructs or properties the prover cannot yet check, are implemented in the `../elisa-engine-proof` worktree (branch `elisa-engine-proof` of `elisa-proof`), each with an accepted and a rejected regression example; `check` uses its `build/elisa-proof` by default. Before each prover change, merge the committed gains of the other `elisa-proof` branches (`main`, `codex/wasmbrowser-proof`) so engine proofs run on the current prover.
8. Completion requires a public API used by real gameplay/editor code, positive and adversarial tests, native evidence where applicable, and documentation. Mocks validate contracts but cannot establish library or GPU integration. Tests must assert outcomes. Write proofs alongside implementations: each slice adds or extends an implementation-linked proof in `proof/` for its pure policy (bounds, wrap/clamp arithmetic, identity and ordering, capacity, state transitions). The proof includes the real source, and shared pure transitions carry their own `requires`/`ensure`. `check` runs every `proof/*.elisa` and records each report. A property the prover cannot yet express becomes a prover task, not an omitted proof.
9. Record performance claims in optimized builds: warm-up policy, hardware, scene size, CPU/GPU time, p50/p95/p99, allocations, and peak memory. Qualifiers and block syntax should add no runtime machinery; verify generated code when that is the claim.
10. Commit only when the same commit includes functional source changes; keep documentation and regression controls with that source slice. Leave plan-only and evidence-only updates uncommitted. Change `[ ]` to `[x]` only with commit, command, result, artifact path, and limitations recorded in a linked `docs/validation/` note. Update capability labels precisely. Commit coherent changes. Do not mark a subsystem complete from its first smoke test. The note also names the slice's proof file, its obligation count, and what remains unproved.
11. If blocked, record the exact cause and a concrete prerequisite task, then continue another ready task. Hardware-unavailable checks remain unverified, never silently green. Product decisions such as the project's license do not block unrelated implementation.

## Active delivery queue — highest return first

This queue controls execution order. The full subsystem backlog remains the
completion scope. Rank work by demonstrated release blockers, affected clients,
reusable fixes and a decisive acceptance result. Finish one bounded source slice
with its implementation-linked proofs and adversarial controls before starting
another. Commit documentation and tests together with source changes.

### Current evidence and open gates

The detailed compiler/runtime evidence, exact source and binary identities, and open-gate status are maintained in [IMPLEMENTATION_EVIDENCE.md](IMPLEMENTATION_EVIDENCE.md). The execution order and acceptance criteria follow below.

### Immediate high-ROI execution order — 2026-10-09

1. **Qualify the newest default-grant compiler/runtime tuple.** Preserve
   default `Global.Read/Write` enforcement and the explicit `-permissive`
   bypass. On compiler source `06e94584`, Stage1 `6fcfb451…` passes 42 actual
   CLI grant controls, 89/89 strict engine entrypoint checks, all 216 uncached
   engine tests and the nested global-rehome O0/O2/ASan stress test. Finish
   pinned-product Stage0/main parity, self-host, JSON O0/O2, deterministic
   emission and proof integration before promoting the tuple. The previous
   broad parity attempt mixed products and was interrupted; its results are
   diagnostic only.
   A new pinned comparison reproduces all 28 AST differences on fresh GitHub
   `main`, so this emitter has no candidate-only regression.
2. **Rebuild and verify the same Studio app.** Matching-bundle crashes faulted
   at `Studio.clip.rehome+1160`; the focused current-product stress case now
   passes, so verify whether that compiler repair resolves the consumer failure.
   Keep the earlier callback-region failure separate. Build the matching Studio
   bundle and test import, drawing, playback and read-only observer reveal before
   changing GLB ownership; fix only defects reproduced by that consumer.
3. **Close proof admission and replay on that tuple.** Keep all 265 obligations,
   five CLI regressions and 73 reports, independently replayed within the original
   memory and time limits.
4. **Integrate and finish qualifying the authority-check optimization.** The
   matched same-source cold check improved wall time by 63.1% with effectively
   flat peak RSS. Complete source review and parity, and keep implementation and
   regression controls in one source-bearing compiler commit. Preserve default
   denials and the explicit `-permissive` bypass. Incremental reuse is a
   separate, still-unmeasured deliverable.
5. **Qualify incremental semantic reuse.** Start after the shared checker change;
   keep reuse disabled until cold/warm diagnostics, inferred grants and artifacts
   match with a fail-closed full-build fallback.
6. **Diagnose renderer lifecycle memory.** Wait for the explicit slot handoff,
   run the original full sequence and fix only the measured retaining owner.
7. **Refresh release packages and hosted CI.** Rebuild and relocate Character
   Course and the second ordinary application on the accepted tuple; retain
   offline startup, restart, teardown and hosted clean-checkout evidence.

This order puts exact compiler qualification first because the focused repair
now passes and every consumer result depends on a matching product. The detailed
product identities, commands and acceptance records are in
[IMPLEMENTATION_EVIDENCE.md](IMPLEMENTATION_EVIDENCE.md).

The queue above is the active priority. Exact product identities, historical checkpoints and gate logs are kept in IMPLEMENTATION_EVIDENCE.md and its linked archives; subsystem acceptance criteria remain in the detailed backlog and validation notes.

### Release-work acceptance details

The numbered rows below carry milestone identifiers, acceptance criteria and
completed records. Schedule them using the immediate high-ROI execution order
above, not the row number.

| Milestone | Concrete deliverable and return | Acceptance / stop condition |
|---|---|---|
| 1 | **Integrate compiler default grants — Q01.** Installed Stage1 `1505c598…` and runtime `013d3174…` pass provenance and direct CLI controls, including `-permissive`. Five Course entrypoints, the uncached 216-test engine gate, 89 additional runtime-wrapped entrypoints and the Maze C-ABI probe pass. The runner preflights pass 42/42 CLI controls and 89/89 strict source checks; all 20 Python integration controls pass. A repository scan finds `-permissive` only in grant preflight/test controls and documentation, not normal engine build/run invocations. The separate local callback-branch pair `1621d550…` / `de964b7e…` passes provenance, both grant preflights, and all 216 uncached engine tests. Candidate compiler source `09f15cf8` defers type queries in generic region analysis, skips duplicate scans for already-required carriers, and avoids full type inference for resolved calls. Stage A passed 5/5, its Stage1 built Gen2 in under 3 minutes versus about 3m50s on current main, and focused O0/O2/ASan controls pass 42/42. Stage B timed out at 600 seconds (exit 124) without a Gen3 object; the harness reported `baseline 0` and `bootstrap closure BROKE`. The exact current-main seed reaches the same limit, so this is a matched timeout rather than a candidate-only regression. Stage C and the self-host fixpoint remain unqualified; use a trace/profile to classify the long run, then set a justified bound if it is healthy work and finish Stage B/C without rebaselining. A previous normal-CLI phase log showed 66 semantic passes taking 12.2s, including 2.3s for declaration resolution, against a 14.9s previous full-check median; the routes differ, so repeat the matching CLI measurement after the carrier change. The 32-job retry was a duplicate and stopped before parallel object emission. Those Linux 42/42 and 89/89 results predate the current grant migration. Its earlier JSON API O0/O2 audit found 884 missing local grants across 250 functions in 14 standard-library modules. The first generated scopes split an if/else and were reverted. The narrowed function-local grants now clear the missing-grant findings in the edited libraries. Adding LLVM 21 to the remote host's PATH allowed the Stage0 JSON archive to build; its log has 40 redundant-grant warnings in compiler sources. The Core runtime mirror is confirmed in the shipped closure: `scripts/build_runtime_object.sh` compiles the vendored tree, `scripts/install_stage1.sh` snapshots it, and `scripts/check_runtime_drift.sh` checks it against Elisa-core. Core commit `0076b655` now mirrors the five JSON parser grant rows already present in the Stage1 checkout. The historical 546-warning / 19-file audit must be refreshed after this sync; migrate the remaining function effects and qualify strict O0/O2 checks after the active compiler slot is handed off. The parser lowers a direct protocol parameter to a synthetic generic such as `__protocol_arg_0`; its bound is stored in the generic side table, but the checker searched enum annotations. The owner now resolves that exact bound and treats a method as pure only when its protocol declares it. The repair is committed as `5be39b79`; all three focused `distance_to` calls and 111 authority cases pass, and the void-poll regression passes at O0/O2 and ASan. Temporary tracing is removed and Stage0 semantic preflight passes. Both targeted regressions pass on the fresh product. The `5be39b79` product passes self-host Stages A-D: Gen3/Gen4 are byte-identical (SHA256 `f3bd017c1dfcfe70b13aaf88de6f73a8b4c623c290d514806c38175a428e9229`) and all 40 repeated emissions match; Stage B took about 13 minutes. Backend-native smoke passes 567/567 compile-and-run checks. The exact-product fast gate exits 1 at `build/global-authority-qualification-5be39b79/fast_gate-r2.log`: AST parity differs in 28/579 fixtures, resolve fixtures lack explicit Global grants, diagnostics diff reports 457 differences, semantic-internal diff has 63/3441 mismatches above baseline 54, and semantic-acceptance diff has 4/587 disagreements. `global_permissions_smoke` passes. The matched main comparison finds the same AST, resolve, semantic mismatch sets and diagnostic identities except one candidate-only effect-row diagnostic where main reports memory growth; the owner is tracing it. Carrier-count and matched runtime comparisons remain pending, so candidate proof and consumer results are provisional. Hold promotion until the candidate-only effect-row mismatch is fixed and parity rechecked, current JSON O0/O2 gates pass, carrier counts and matched runtime comparisons are recorded, and proof replay and engine gates pass. Maze passes headless gameplay, native SDL3/Metal build/run and all nine relocated-package controls. Environmental-effects and physics-interactables pass strict checks and their native smoke/self-test on the saved `b11e9121` Stage1/runtime pair; see [example grant evidence](docs/validation/global-grant-public-examples.md). | Reduce the 40 redundant-grant warnings, refresh the historical Core runtime audit on the current strict compiler, and migrate remaining function effects in canonical and vendored sources together. Require `check_runtime_drift.sh` plus strict O0/O2 gates before promotion; do not weaken grant enforcement. Rebuild and independently replay the proof product on a matching compiler/Core/runtime tuple; retain all 265 obligations, five CLI regressions and 73 engine reports. Run the full gate on the repaired performance candidate and repeat Course/Studio consumer checks after compiler and parity qualification. |
| 2 | **Profile and optimize semantic-check throughput.** The matched Stage0 O0 `-emit check` completes at exit 0 in 2,169.7 seconds wall / 2,193.8 seconds CPU, peaking at 1,626,628 KiB; `check_mutable_global_authority` alone takes 2,007.0 seconds (~92.5% of wall). Frozen-run timings now report authority report traversal at 25m50s versus 5m35s for inference; read/write closure took 2.4 ms and effect-row closure 12.197 s. The traversal had reached 7,168 functions at about 23m22s, while authority-closure spans remained small. The exact matched Stage1 O0 check improved from 2,169.739s wall / 2,193.822s CPU on Stage1 `d0a9d62f…` to 800.425s / 827.244s on Stage1 `6b3ea99d…` (63.1% less wall), with peak RSS effectively flat; source, runtime and command identities are recorded in `IMPLEMENTATION_EVIDENCE.md`. This is a cold semantic-check gain, not incremental-reuse evidence. A same-source full-LLVM pair took 156s with the candidate and 149s with main. Across two pairs, the candidate averages 158.7s and main 155.7s, about 1.9% slower with substantial run-to-run variation; this neither establishes a slowdown nor clears the no-slowdown gate. The owner traced an extra carrier on `arena_dict_new__i64` to treating any hidden generic ABI slot as a local result region, and narrowed insertion to true caller-result slots. Focused explicit-arena and growable-reference controls pass at O0, O2 and ASan; broader parity, carrier-count and matched-runtime gates remain pending. The compiler suite also found that concrete generic `darray.store_get()` missed protocol dispatch available through `[S: Store]`; its isolated Core fix passes focused semantic and positive native tests, including a check that a signature grant cannot authorize the function body. The out-of-range control passes with unbuffered output and asserts both `SIGABRT` and the exact panic message. The optimized Stage1 and same-input semantic benchmark are now qualified for the cold-check result; broader Core/runtime and compiler acceptance remain pending. Repeated permission-parameter scans were the measured hot path, and the shared-checker cache reduced the report walk from 25m50s to 321.023s. Carry these precomputed permission facts into incremental replay; do not duplicate grant rules. After r5's focused collector pass, the owner froze the source copy; its 644-file import closure matches r5. The O0 Stage1 seed and frozen check-only pass were started from pinned Stage0. The baseline report walk took about 25m50s; the candidate report walk completed in 321.023s, and the full check exited 0. Keep the shared checker as the single source of truth; any incremental candidate must reuse its precomputed facts and preserve default grant denials plus the explicit `-permissive` bypass. A previous pinned attempt stopped at 23m54s CPU; retain it as a stopped diagnostic, not the baseline. | Pin the same source, Stage0 compiler, command, runtime and host; record subpass counts/times. Optimize only measured repeated work. Re-run the same check-only command, preserve grant denials and `-permissive`, and report total wall/CPU/RSS plus phase timings. |
| 3 | **Broaden incremental compiler reuse after the focused fail-closed pass.** The r5 combined O0 object compiled and linked, and the native runner exited 0 across owner-index, callback-origin, snapshot-codec and collector assertions. It fixes r4's over-conservative rejection of literal local binding `hot = 3`; shadowed calls and mutable aliases remain on the fresh-check path. The malformed-surface control marks all four function records uncacheable with `InvalidTokenSpan` and emits no direct facts. Four native checks for lookup indexes, snapshot encoding and reusable function facts now pass. Separate disk-persistence qualification passed 15 actual-process cases covering short reads/writes, corruption, oversized files and failed-write cleanup. The combined standard-library edits caught two syntax errors; both are fixed. The current combined runner compiled and linked, then exposed an assertion expectation error: after invalidation the original permission-annotation scan correctly returns true, while the assertion expected false. The assertion now checks agreement with the original scan; corrected combined rerun evidence is pending. Earlier same-host runs overlapped the frozen timing measurement; the shared checker must first remove repeated grant-annotation scans because saved body facts still invoke them. The prototype now evaluates one semantic-option tuple for analysis and its future cache key, and maps expanded-source declarations to original file paths. Controls for duplicate include resumes and invalid source maps have been added but are not yet reported passing; static-generated source remains uncacheable until it has a matching map. Direct replay is now implemented for effect rows, read/write requirements, both dependency graphs and diagnostics. Its next native gate must compare those outputs with the existing checker. Shared-checker trace and grant-source controls remain pending; production reuse stays disabled. A matched cold build spent 33 of 36 minutes checking global authority. This qualifies focused controls only. The prototype is not production-wired, has no measured speedup, and covers a small fraction of compiler functions. Next target unchanged functions’ semantic-analysis reuse, recompute affected callers and permission propagation, and keep object caching and broad parallelism deferred until cold/warm diagnostics and inferred grants match. | Freeze and pin the tested source/product. Compare the matched check-only workload, then qualify row updates, collisions, fallback, checker-added rows and grant propagation against clean builds. For no-op, body-only and interface/effect edits, report cold/warm timings, cache hits/invalidations, diagnostic and generated-artifact parity, with a safe full-build fallback. |
| 4 | **Qualify proof grant admission and replay — Q01.** The proof branch has focused Global grant fixtures; a source audit found seven proof-owned mutable globals with identified direct access locally scoped. A strict compile with local callback-branch compiler source `c3fc94b1` still surfaced proof functions needing narrow grants. The proof owner applied scoped grants to 44 functions across 13 modules. Initial O2 builds exposed the in-place void call shape, a missing final carrier_surface argument, and missing local Global grants. The wrappers now pass false, declare and scope Global.Read/Write, and propagate those effects to callers. Strict O2 succeeds and proof source is committed as 5a12c8fd; the rebuilt committed-tree product passes prover freshness. The matching Studio O2 build exceeded its default 4 GiB guard at 4.25 GiB; its 8 GiB retry was stopped after ten minutes in LLVM’s AArch64 DAGCombiner path, with no object or diagnostic. O1 remained in that path. O0 codegen packaged an inspection app; 23 test targets failed, many with grant diagnostics, The post-build proof pass was stopped after multiple laws exceeded 13 minutes; it reported grant admission failures and missing or unreviewed proof baselines, so full replay is unqualified. These checks used Stage1 `09f15cf8`, before the separate effect-owner index candidate now under parity review; rerun with that indexed product before source migration. The app is running, but no playback/reveal acceptance or optimized build is qualified. Studio acceptance and full proof replay remain open. The earlier ten-missing-grants result came from a different compiler snapshot and is superseded; no current proof/compiler pair or full replay is qualified. The 25-obligation diagnostic harness also records the builtin-worklist/custom-pop memory reduction from 757,284,864 to 16,089,088 bytes. | Build the updated proof source with the exact promoted compiler/Core/runtime pair, pass default grant admission on every public proof API, then replay all 265 obligations, five CLI regressions and 73 reports within the original 3 GiB / 120-second limits. Preserve the 4,246-obligation inventory, immutable snapshot census/matrix, and classification of all nine historical timeouts. |
| 5 | **Fix and qualify global rehome for heap-owning values.** The Studio crash reports fault at `Studio.clip.rehome+1160`; the consumer trace implicates `publish_candidate` moving an affine `Clip` with nested GLB document buffers into a global, but current-candidate replay is still required to establish the cause. The preserved 512-element `nested-global-rehome.elisa` source (SHA256 `cd85877fb1bd55d84404ba0056b6d6bc97e1c09fe616018b36614cff175895c3`) exits 1 at O0/O2/ASan on older product `607f7168…`; it contains no GLB document. Current compiler candidate `ae59c06b`, Stage1 `4cb61841…`, passes self-host A-D and backend-native 567/567. Its fast parity profile has passed source, annotated-list, C-bind, dependencies, docs and format; AST's 28/579 set matches the current-main gate log by fixture name only; that log omits product hashes. Header parity completed in 1,776s with 34 divergences; its failed-name set matches the older main header log, but neither the main Stage0 nor Stage1 hash is recorded. Interface parity reports 61/333 differences with the same failure set as the older main log; neither product identity is recorded, and the current Stage0 files at the two paths have different hashes despite sharing a source commit. Fresh matched-product comparisons remain open. Fresh consumer FBX-to-GLB headless playback completes in three views, while exact-bundle provenance, GUI playback and the separate 337-frame take remain open. See [compiler and consumer evidence](IMPLEMENTATION_EVIDENCE.md). | Replay the preserved stress fixture on the exact candidate with strict default grants, compare all failures with the exact parent baseline, then rebuild and retry the same Studio bundle. Keep the callback hidden-region repair separate and qualify its UAF, carrier, hidden-region and result-lifetime controls. Verify import, all 337 frames, constant channels, redraw, malformed-input refusal and read-only observer reveal before closing consumer acceptance. |
| 6 | **Qualify callback-region ABI/result ownership separately — Q01/Q03.** An earlier import crash showed `path_copy` losing its hidden arena through a generic callback. The `b719dbd5` repair and Stage1/runtime provenance are recorded; the latest `Studio.clip.rehome` reports do not qualify that repair. | Pass focused UAF/scalar-carrier, large-aggregate, hidden-region and result-lifetime regressions, then retry the original import. Keep O2's AArch64 DAGCombiner stop as a separate backend gate. |
| 7 | **Complete current-tuple FBX playback acceptance — Q01/Q03.** Consumer commit `1e98f075` aligns multi-key FBX tracks; its bounded bridge regression passes on the fresh compiler/runtime tuple and preserves source bytes. The fresh FBX-to-GLB headless playback harness now completes the full animation with skinning and drawing in three views; the current `build/MocapStudio.app` has been relaunched, but GUI picker/playback and exact bundle provenance still need qualification. | Use the relaunched app to verify the separate 337-frame take, constant channels, repeated redraw, malformed-input refusal and observer reveal. If the GUI path fails, repair only the reproduced defect; the headless result does not close app acceptance. |
| 8 | **Qualify actual Studio observer reveal.** The consumer source now has the native binding and bounded version-2 decoder, and its build script includes the three observer translation units from engine-mocap. | Validate original, quarantined, conflicting and uncertain locations in the actual Studio reveal flow after the receipt is rechecked. Unknown evidence must reveal no path; observation must not grant restore or deletion authority. |
| 9 | **Resolve renderer lifecycle footprint — R17/Q01.** The full native sequence has an intermittent footprint increase with no matching heap/GPU growth. The VM-region inventory is rehashed at engine `fa451328`; its process-group watchdog enforces aggregate RSS/time budgets and writes separate diagnostic evidence. | After explicit Studio/UI slot handoff, run the exact prepared command with one job, the original warmup/cycles and 8 MiB allowance. Inspect the real peak cycle before changing resource lifetimes; a retry alone is not a diagnosis. |
| 10 | **Refresh and relocate Character Course — Q02/Q04.** All five current public entrypoints pass strict project-context checking on the pulled compiler after local grant adoption. The previous optimized package predates this compiler/source tuple; rebuild and package once the current compiler pair is promoted. | Generated outputs match; resource hashes/notices are complete; relocated offline startup, restart and teardown pass with source/Homebrew denied. Keep signing, legal and separate-machine acceptance open. |
| 11 | **Qualify hosted clean-checkout CI — Q03.** The `b11e9121` compiler pin matches the verified upstream `main`; the lock records all four observed heads, CI checks for drift, and hosted execution stays blocked until proof qualification. Core/proof/ElisaScript pins lag their heads and remain unchanged until the exact tuple qualifies. | Pass strict proof build/replay, the original 265 obligations, five CLI regressions and 73 engine reports on one tuple, then run hosted clean-checkout CI with retained provisioning/build/proof/package artifacts. GPU qualification stays separate. |
| 12 | **Finish gameplay and physical acceptance — Q07a/Q06.** A visible run of the October 7 Character Course package now held crouch through the tunnel, lit all three beacons and reached the summit; the C binding was restored afterward. The hidden SDL route already covers jump, restart and fall. This manual result uses an older non-optimized package, so it does not qualify a fresh build on the current compiler tuple. See [live input and frame-clock evidence](docs/validation/character-course-live-input.md#manual-live-traversal-2026-10-09). | Rebuild on the accepted compiler/runtime tuple and verify the fresh package's complete gameplay loop, reload/restart/teardown baselines, visible jump/win/fall, and available controller/audio paths. Physical checks require hardware evidence; unavailable hardware does not block ready local work. |
| 13 | **Restore the source-length gate — complete in `9e40976a`.** Split the three over-limit Python files by responsibility and fingerprint the extracted asset-cook configuration module. | `python3 scripts/check_source_length.py` passes; 41 packaging, 48 build/run and asset-cook, and 2 dense-cook tests pass. Full details and log paths are in [validation](docs/validation/source-length-policy.md). |
| 14 | **Rehearse the existing Maze project as the second ordinary application — Q07a/Q02 — acceptance complete.** The existing public-API client exposed and now scopes the default `Global.Read/Write` grants required by world construction, scene presentation and native runtime calls. On saved compiler source `b11e9121` and its matching runtime, the headless game route, native SDL3/Metal scripted app, all nine relocation/package controls and packaged-shader cold/warm/invalidation controls pass. Exact commands, compiler/runtime/artifact identities and limits are in [Maze grant acceptance](docs/validation/maze-global-grants.md). | Completed source slice: `examples/maze/main.elisa`, `examples/maze/native_client.elisa`, all three native entrypoints and `scripts/maze_game.elisascript`; real executable and cooked bundle hashes are retained in the validation note. The independent fresh Character Course package, hosted CI and compiler/proof gates remain open. |
### Ordinary-project preparation

Executable output admission now rejects existing FIFOs and Unix sockets before
compilation, preserving their inode and type. The focused build/run collection
passes 47 controls (`build/validation/build-special-output-controls.log`). This
closes a demonstrated destination-validation gap; actual fresh optimized package
and public-API application acceptance remain open.

Executable output now refuses entry-source and project-manifest collisions,
including filesystem aliases, before invoking tools. All 36 build-runner controls
pass; author files remain unchanged ([evidence](docs/validation/project-output-collisions.md)).
Console builds now preserve the previous executable after compiler failure or
success without output; successful builds publish by replacement. A fresh real
O2 compiler build and failed rebuild retain identical runnable bytes
([evidence](docs/validation/console-build-publication.md)).
Cook declarations also reject existing non-file outputs before any cooker or
publication, preserving companion outputs and cache state
([evidence](docs/validation/cook-output-file-types.md)).
A failed later cook-output replacement now restores earlier outputs and preserves
cache records; failed rollback retains recovery backups. Backup cleanup refusal
also preserves the completed transaction result and retains previous bytes; the
combined cook/build controls pass 50 tests. The actual optimized
image-cook/cache rehearsal passes on this source
([rollback evidence](docs/validation/cook-publication-rollback.md)).
Native application builds now validate and prepare Wicked runtime links before
publication, publish the executable last and roll back changed links on failure.
Five new fault controls join the 41-control build suite, including retained
recovery backups when rollback fails
([native publication evidence](docs/validation/native-build-publication.md)).
Provenance now joins that rollback transaction: staged bytes supply the hash and
size, the final output supplies the identity, and manifest preparation failure
preserves the previous publication. All 48 build/provenance controls pass.
macOS packaging also refuses declared resources that would overwrite the generated
executable, including case variants and descendants. Existing regular-file bundle
destinations fail before assembly; backup cleanup cannot report failure after
successful publication. Declared resource paths also refuse parent symlinks before copying, preventing
external files from entering the bundle through a leaf-only check. Explicit cooked
resource declarations now merge with automatic staging from the same source. All
41 package controls pass, including fallback directory/file symlink refusal and
ignored-litter exclusion, with the previous bundle and author resource preserved
([evidence](docs/validation/package-executable-resource-collision.md)).
Cook output collision checks now include glTF/GLB external buffers and images,
using the same local-resource discovery as cache fingerprints. A reproduced
cross-cook overwrite of an authored glTF image is refused before any cooker runs;
48 build/run controls pass (`build/validation/cook-referenced-source-controls.log`).
The full fresh public-API author/cook/package rehearsal remains open.

Bounded Q02/Q07 package relocation is now exercised with the existing
physics-interactables native self-test executable. Its manifest explicitly
declares no runtime asset files, and package provenance captures the separate
assembly-manifest identity, shader settings and complete bundle payload
inventory. Two relocated launches pass with source and Homebrew access denied.
This uses a saved prebuilt executable and does not close fresh author/cook/build,
interactive gameplay, signing/legal, clean-machine, or current compiler/proof
acceptance ([evidence](docs/validation/physics-interactables-package.md)).

### Bounded consumer work while release gates run

Mocap-cleaner requested the read-only
`elisa_studio_build_trash_observe_locations_v1` observer. Native implementation and
focused identity, package-layout, path replacement, readonly-operation and uncertain
publication controls are qualified in engine-mocap `3f60d5ed`. Consumer source
now has the version-2 native binding and decoder, and `build_studio.sh` includes
all three observer translation units. The remaining deliverable is actual Studio
reveal acceptance, not API registration. The exact-current optimized app build
has passed freshness and ABI preflights; its first O2 attempt hit the 4 GiB guard
at 4.25 GiB. The owner stopped the 8 GiB O2 retry after ten minutes in the same
LLVM AArch64 SelectionDAG/DAGCombiner path, with no object or compiler diagnostic;
the 2-second profile placed 177 of 178 samples in `FoldingSet::NodeEquals` during
`ReplaceAllUsesWith`. O1 also remained in the DAG-combine path; the owner stopped
it. O0 codegen completed and packaged `MocapStudio.app`; the app is running for
inspection; its main thread is in the AppKit event loop with no new crash report,
and the original FBX digest is unchanged. Window automation timed out while
attaching, so playback is still unverified. The build reported 23 failed test
targets, many with explicit Global
grant diagnostics. The post-build proof pass was stopped after multiple laws
exceeded 13 minutes; it reported grant admission failures and missing or
unreviewed proof baselines, so full replay is unqualified. The O0 artifact uses Stage1 `09f15cf8`, including `806772c7` for the
global-rehome repair; it predates the separate effect-owner index candidate now
under parity review. Rerun the grant/proof gates with that indexed product before
treating these as consumer source gaps. These results qualify O0 code generation
and app launch only; optimized
app, proof, playback and observer acceptance remain open. Profile the exact
generated function behind the O2/O1 hotspot before changing compiler or consumer
source, then rerun the optimized build after a targeted repair.

Exercise the bounded wire format, full retained binding and immediate reobserve
before reveal. Keep uncertain paths empty. Exact directory/lease identity does
not authenticate product contents or authorize restore or deletion. Acceptance
requires actual Studio results for original, quarantine, conflicting occupant
and uncertain locations. See the [coordinated M track](docs/plans/mocap-engine-track.md).

### Immediate source slices and handoffs

1. **Studio integration:** two Oct 9 reports match the current O0 bundle UUID
   and fault at `Studio.clip.rehome+1160` while drawing; the consumer owner is
   tracing an invalid imported-GLB byte buffer, with the causal ownership
   transition still open. The chooser currently leaves Open disabled with the
   FBX selected, so playback cannot be repeated through that flow. Separately,
   an earlier import report exposed a 9,232-byte `Job`/hidden-region callback
   failure; qualify its compiler repair with focused regressions rather than
   assuming it explains the current rehome crash. After both gates are resolved,
   exercise the exact bundle from sealed inputs and repeat from clean inputs
   before claiming reproducibility. Migrate only caller chains or FFI effect
   boundaries exposed by those runs. Two remaining Studio fixture build failures
   traced to missing local grants around scene/model calls; grouped grants are
   now added in `mocap-cleaner/test/studio_capture.elisa` and
   `test/studio_correction.elisa`. Commit `a21c2e75` contains these source
   changes; the seven focused Studio fixtures now pass on the r3 compiler
   product. Repeat them after the outstanding protocol-effect correction, then
   continue to the sealed app. The O2 attempt on compiler `9d2cf1b` stopped in
   AArch64 SelectionDAG/DAGCombiner without an object.
2. **Engine owner → mocap consumer:** after the current backing-store crash is
   resolved and the callback ABI regression passes,
   use clean integrated dependency `dc180e49` (mesh repair retained) for actual
   Studio redraw and observer registration/reveal acceptance. Local mesh source
   acceptance passes; avoid handoff-only changes or dependency moves that do
   not advance this gate.
3. **Compiler/prover owners:** retain the completed `e791ec50` allocation and
   custom-pop repair and its passing semantic, seed and O0/O2 ownership controls.
   Integrate the qualified default-grant repair and build the latest clean prover
   source (`6c5f4e5b` or its authenticated successor). The next decisive result is
   `proof/action_input_context.elisa` under its original 3 GiB / 120-second budget,
   with all 265 obligations proved and independently replayed. Then run the five
   original CLI regressions and all 73 engine reports, retaining the original
   4,246 obligations. Run the immutable census/matrix only after these pass.
   The 25-obligation memory improvement is diagnostic evidence; do not repeat
   ruled-out borrowed-array or prover-source hypotheses, or claim full acceptance
   from it. Preserve separate explicit compile-slot handoffs.
4. **Native runtime owner:** capture the failing lifecycle memory domain and
   identify the owner retaining it. Change the release/lifetime path, then exercise
   the original full sequence. Stop unchanged retries that add no diagnostic fact.
5. **Mocap integration owner:** exercise actual Studio reveal acceptance for
   original, quarantined, conflicting and uncertain locations. Read-only
   observation must not become restore or deletion authority.

Hold heavy census/native runs while the coordinated live app checks are running
or compiler inputs are changing. Exact callee, callback and binding-scope repairs
require qualification before consumer acceptance. Start another compile only
after an explicit terminal slot handoff; a process snapshot does not establish
availability. The interrupted snapshot census is incomplete evidence.

### Start next: one ready task, one decisive result

| Readiness | Task | Why it leads / next action |
|---|---|---|
| Current Studio crash and proof freshness remain open | Resolve the matching-bundle `Studio.clip.rehome` crash, qualify the earlier callback-region path separately, refresh and authenticate the proof pair, then run playback/redraw/reveal acceptance. After explicit slot handoff, run `physics_rig` and the latest Studio character regression sequentially. | Mocap source commit `b79608a7` and UI commit `65f370f3` passed strict app semantics on the earlier snapshot. The clean `19294e83` compiler product passed detached provenance, but its tuple had six codegen declines and the proof frontend pin `2a3dce66` was stale. A newer O0 diagnostic bundle exists and matches the two crash reports at `Studio.clip.rehome+1160`; it is not accepted. The shared compiler checkout has uncommitted generic-codegen edits and must be reseeded before they can qualify. No physics executable is qualified. |
| Backend repair complete; current-source prover pair required | Qualify repaired prover memory on original engine input | Integrate default-grant compiler with backend `e791ec50` and latest clean prover repairs, then run unchanged first input under 3 GiB / 120 seconds. Follow with five CLI regressions and all 73 engine reports; diagnostic-harness success does not close these gates. |
| Consumer integration ready | Actual Studio mesh redraw and observer reveal | Use the current mesh repair and retained observer-header identity; validate the public API in the real consumer. |
| Ready without a heavy compile slot | Package identity and relocation rehearsal — Q02/Q07 — bounded acceptance complete | The physics-interactables native self-test now packages from its explicit empty resource allowlist; its package manifest, shader settings and 406-file bundle inventory are recorded. Two source/Homebrew-denied launches pass. The fresh public-API author/cook/build/package workflow remains open and should use a compatible accepted compiler tuple. |
| Ready after coordinated heavy slot | Renderer lifecycle diagnosis and repair | Observe the failing full sequence, identify the retained owner/domain, then repair its lifetime. Keep original cycles, warmup and 8 MiB allowance. |
| Ready after compatible tuple and native gate | Fresh package and ordinary author workflow | Reuse one clean optimized build for Character Course relocation and the existing Maze public-API application. Console image cooking and native package reading are preparation; the packaged application remains open. |

While Studio and prover acceptance run, select source work with an independent
acceptance path. Completed publication safeguards need actual package consumption;
additional fault controls alone do not outrank release acceptance.
Reserve each heavy slot through the existing coordination; record its
explicit handoff and terminal result in the validation record, since slot ownership
changes during execution. Defer census, broad native retries and package rebuilds
until their prerequisite can produce a meaningful acceptance result.

### Historical checkpoints

Superseded compiler, consumer and toolchain snapshots are retained in IMPLEMENTATION_EVIDENCE.md and its linked archives. Do not use historical results to close a current gate.

### Choose work by unblock value

| Decision | Action |
|---|---|
| The committed Studio build is sealed | Run the actual FBX/redraw/reveal workflow and fix only reproduced consumer defects. |
| The latest compiler-backed prover pair is ready | Run the unchanged original input and full engine acceptance before any compatibility census. |
| A heavy slot is handed off | Run the original acceptance workload next; prefer a release gate over another reduced fixture. |
| A package or second application exposes a missing public service | Promote that specific F–X task with a consumer and end-to-end acceptance; avoid implementing unused subsystem breadth. |
| Completed safeguards already pass focused controls | Consume them in real builds, packages and hosted CI. Reopen them only for a newly observed failure. |

### ROI and exit rules

**Defer until these gates close:** additional dependencies, editor-shell breadth,
multiplayer, platform expansion, style-only sweeps, repeated evidence refreshes
and full compatibility censuses with the known memory failure. Keep the F–X
backlog as completion scope; choose its next feature from a demonstrated gap in
the second ordinary application.


- Before starting a slice, record its consumer, observed failure, owning source,
  prerequisite and one decisive acceptance result. Prefer shared unblock value
  and short feedback over adding another implementation that remains unused.
- Set a diagnostic stop condition: if a comparison rules out the hypothesis,
  retain the evidence, remove speculative source edits and choose a new cause.
  Repeated evidence refreshes are not implementation progress.
- Choose the smallest source change that closes a measured failure or unblocks
  multiple real clients. Name the failing case, owner, acceptance command and
  expected outcome before implementation.
- Complete and integrate working repairs before adding features. Do not repeat
  already passing mesh, cook or publication work unless the consumer exposes a
  new failure.
- Stop a failed gate at its first actionable cause, preserve its records and
  minimize that cause. Run the complete original gate after the repair; reduced
  examples never replace acceptance.
- Bundle tooling, documentation and regression controls with their functional
  source slice. Do not create a commit just to refresh evidence or this plan.
- After these release gates, use the fresh second application to select the next
  public-API gap from the full backlog. A named authoring obstacle or measured
  runtime budget outranks speculative subsystem breadth.

### Selection rules

- Prefer a task that closes a demonstrated release gate or unblocks multiple
  consumers, has a bounded source change and produces decisive acceptance evidence.
  Treat compiler-owner waits as a dependency; advance the next ready slice.
- Treat integration and qualification as deliverables with the original acceptance
  criteria. Once a causal repair passes its focused controls, move to the original
  workload instead of creating another reduced example or evidence-only slice.
- Keep one source slice in progress per owner. Diagnostic documentation, compiler
  style sweeps and additional proof examples do not outrank an unresolved gate.

- Keep qualification source and toolchain immutable. Set proof and census worker
  concurrency explicitly within the original RSS cap; keep independent native
  work from competing with the measured gate.
- Reproduce and minimize a failure before choosing a fix. Prioritize a shared
  source/replay defect over another standalone proof feature.
- Do not rerun unchanged expensive benchmarks or native retries to accumulate
  evidence. Repeat after a source repair, changed instrumentation, or a specific
  diagnostic hypothesis with an observable result.
- Preserve contracts, refusal controls, source admission, proof inventories and
  original budgets. Never drop failing rows or raise tolerances to make a gate green.
- Apply compiler style to touched code. Defer broad rewrites, new showcases and
  subsystem wrappers unless a named consumer or measured shipping blocker needs them.
- After a slice passes, advance to the next ready item. External hardware, signing
  and license decisions do not block independent local work.

## Deferred work and promotion triggers

These tasks remain useful, but are outside the active queue unless they unblock an
acceptance criterion above. Existing implementations and regressions remain maintained.

| Backlog | Promote when |
|---|---|
| Additional renderer passes/quality modes (R05–R07/R09/R15–R20) | The playable client has a specific visual defect, required missing feature, or measured GPU/startup bottleneck. Preserve current effects rather than adding showcase breadth. |
| Streaming, terrain, parallelism and scale (W05/W07/W10, A10/A11, R10/R11, C09) | Representative content exceeds a recorded memory/frame/loading budget; start with the measured bottleneck. |
| Full animation graphs, IK, retargeting and specialist audio (C03–C07, S04) | The game's motion or sound requirements cannot be met by the validated simpler path. |
| Editor suite, advanced widgets and text authoring (E01–E10, I04/I05/I07–I10) | A repeated authoring workflow or an actual text/localization requirement blocks the game. Basic accessible menus remain active above. |
| Multiplayer/GNS (T01–T08), additional OS targets (Q08), Godot feature parity | A concrete target game/platform requires them after the native single-player package works. Shared-contract compatibility fixes remain in scope. |
| Box2D, ACL, vehicles, XR and other specialist integrations (P09–P12, X01–X07) | A named consumer and acceptance workload justify the dependency and maintenance cost. |

**Milestones:** first a repeatable existing-game gate; then a packaged second game;
then durable scene/resource ownership; then character/audio/navigation depth justified
by that game. Editor and large-scale/network milestones follow demonstrated demand.

## Full subsystem implementation scope

The [detailed implementation backlog](docs/plans/implementation-backlog.md) contains
all F–X requirements, dependencies, acceptance criteria and historical evidence.
It remains part of this plan and the goal's completion scope. The active queue
above orders delivery; it does not replace or remove the subsystem backlog.

## Validation and handoff

Use the existing commands as regression anchors:

- `elisascript scripts/check.elisascript` — shared runtime, rejection, proof, package, and Godot compatibility checks.
- `elisascript scripts/native_gate.elisascript native` — native-only startup, application, SDL3/Wicked, and library integration gate with structured evidence.
- `elisascript scripts/wicked_probe.elisascript` — focused real native graphics and library checks; requires the configured Wicked build and a suitable graphics session.
- `python3 scripts/run_boundary_sanitized.py` — untrusted native boundary checks.
- `python3 test/check_workflow.py ~/.local/bin/elisascript` — orchestration behavior only.
- `python3 scripts/check_module_hygiene.py` and `python3 scripts/check_source_length.py` — repository policies, including this plan's 600-line limit.
- When changing compiler field/reference semantics, run the owning compiler's targeted
  regressions and `test/parity/driver_acceptance_smoke.sh`; do not raise its baseline to hide regressions.

Every handoff names: completed task IDs, commits/repos, commands and outcomes, evidence files, API changes, remaining limitations, and the next ready task. Maintain separate statuses for implemented, integrated, tested, proved, hardware-unverified, and blocked. No number of checked boxes substitutes for the milestone's playable and packaged result.
