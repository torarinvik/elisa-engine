# Elisa Engine — native implementation backlog

**Updated:** 2026-10-08. **Focus:** close measured release blockers, then ship reusable public-API games on Wicked + SDL3.
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
   New guide guidance (`e3087e23`, read at compiler `b26659e2`): after qualifying the selected product, prefer comprehensions for touched pure collection builders and labelled loop results for nested searches. Ordinary block initializers may contain loop exits; exits from capture blocks remain forbidden. Triage `push loop` and `value-thread` lint findings; preserve ordering, allocation lifetime and proof replay. Prioritize builders encountered during package refresh and ordinary-project rehearsal; see the targeted adoption rules above.
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

- **Qualified baseline:** compiler `52d60fcf` and matching runtime pass 215
  uncached runtime tests and the shared gate. Production prover `f593c886`
  retains all 73 engine reports / 4,246 obligations; its full matrix failed.
- **Latest isolated proof repair:** clean prover `5c40d273`, paired generation
  `65851d44058c49fca2e0d06974214022`, retains all 73 uncached engine reports /
  4,277 obligations with independent replay and zero diagnostics, gaps or trusted
  assumptions. Original strict-order, signed-constant and return-branch CLI
  regressions pass. Full compatibility remains open; production is unchanged.
- **Current prover blocker:** clean `10ae0267` and immutable compiler
  `fb8e0927` build paired generation `649d72a317c342dba3607ca7e77493b8`
  in 64.92 seconds. Five original CLI regressions pass. Engine inventory fails:
  `proof/action_input_context.elisa` alone exceeds the unchanged 3 GiB cap in
  0.31 seconds. The historical pair processes the same source at 52,416 KiB
  with 265/265 proved and replayed; this comparison diagnoses a regression and
  does not qualify the new implementation. Minimize and separate prover-source
  changes from compiler code generation/runtime before another full run.
  Subsequent fixed-source/frontend/runtime O0 comparison isolates emitted-code
  behavior: current emitter peaks at 757,284,864 bytes versus 17,203,200 bytes
  historically on a self-contained 25-obligation case. Recent prover changes and
  a borrowed-array hypothesis are ruled out; 90,033 allocations of 8 KiB dominate
  current cumulative allocation. Repair the owning compiler's allocation/lifetime
  path, then repeat the original engine input and inventory; mixed historical
  products are diagnostic only. Frame-pointer attribution now identifies
  `proof_kernel_replay_expr_equal`'s 32-byte-pair worklist: its 256-element
  literal allocates 8 KiB directly in the static arena. The method-only repair
  `220ae3f9` passes native O0/O2 controls but leaves the original memory result
  unchanged. Lexical receiver typing for builtin `darray.pop`/`push` was the
  causal follow-up, preserving conservative treatment of custom, generic,
  ambiguous and unknown calls plus genuine global publication. Typed builtin
  repair `d69f219b` and custom-pop dispatch correction `e791ec50` now pass semantic
  preflight and official seed provenance. Three native ownership controls pass at
  O0/O2, including 100,000 worklists, real global publication and retained custom
  receiver storage. The unchanged 25-obligation checker/replay harness peaks at
  16,089,088 bytes versus 757,284,864 before, with source/frontend/runtime/O0 fixed.
  This proves the causal improvement on that diagnostic harness; it emits no full
  normalized JSON, so exact report-byte equality is not established. Current paired
  prover build, original engine input and full inventory still require acceptance.
  Preserve explicit compile-slot handoffs
  ([evidence](../elisa-engine-proof-counterexample/docs/validation/dispatcher-budget-repair.md)).
- **Compatibility throughput:** retention/provenance repairs are committed in
  isolated prover `4d9f3a8d`. A diagnostic census covered 1,174 inputs with nine
  timeouts, but used mutable compiler inputs. Snapshot-backed census/matrix
  acceptance waits for the first-input memory repair; retain every input and
  original budget.
- **Strict-negative repair:** isolated prover `5077910c` evaluates exact,
  non-wrapping unsigned subtraction at widths 8/16/32 for diagnostic
  counterexamples. Focused controls pass, including overloaded-operator refusal;
  the original return-branch fixture replays 23/23 and its strict negative is
  disproved. Clean paired build and original return-branch CLI acceptance pass; full
  compatibility remains open.
- **Consumer mesh bounds:** skin/draw source guards are implemented and pass
  O0/O2 malformed-input controls, focused AddressSanitizer and the existing GLB
  loader regression. All 73 engine reports now independently replay 4,277
  obligations; the original 4,246 are retained. Actual mocap consumer integration
  remains open ([evidence](docs/validation/mesh-overlay-shapes.md)).
- **Native release blocker:** capture and Character Course relaunch have focused
  repairs, but effect lifecycle memory remains intermittently above the original
  8 MiB allowance after the full renderer sequence. Heap/GPU and VM-domain
  diagnostics are present; opt-in region-tag observation now has focused mapping
  and overflow controls ([region evidence](docs/validation/effect-memory-vm-regions.md)).
  No root cause or leak repair is established. Preserve
  the failed full-gate evidence ([native evidence](docs/validation/counted-fill-memory-growth.md)).
- **Compiler policy required:** `global mutable` reads/writes must require
  `Global.Read` / `Global.Write` by default, with explicit `-permissive` bypass.
  Historical candidate `fb8e0927` passes 17/22 CLI controls and accepts all five
  accesses that should be refused without opt-in headers. Default enforcement
  remains unqualified. Exact callee identity and callback analysis also block
  actual Studio acceptance. Engine identity helpers have local grants; promote
  only a clean authenticated product that passes the actual CLI controls and
  owning compiler regressions ([evidence](docs/validation/global-mutable-grants.md)).
  Newer default-enforcement source `f4f0cda8` passes semantic preflight but its
  official seed fails during runtime-object compilation: 312 Global diagnostics,
  unchanged source, 84.64 seconds and 7,051,056 KiB under 8 GiB. No qualified
  runtime/provenance tuple exists. The next compiler source task is conservative
  candidate resolution for
  target-specific lock helpers: multiple static-if variants currently produce
  false read/write requirements despite existing trusted cache blocks. Preserve
  possible variadic, generic and integer-alias targets and fail closed on missing
  summaries; actual profiler and host-callback effects remain required. Backend
  repairs are integrated with this source at `7ff4b840`,
  currently source-only. Resolve this gate before Studio, latest prover pair or
  package qualification; avoid building the superseded `10ae0267` prover as current.
- **Shipping client:** the guide rig refresh is committed at `a30d3af9`.
  Current rig, sound and cell generator checks pass on this checkout. The
  historical relocated Character Course bundle still needs a fresh optimized
  build/package acceptance; older green runs do not qualify the replacement
  toolchain or package.

### Delivery milestones and ranking

Use dependencies and expected unblock value to choose the next task; table order
is not a reason to idle behind another owner. The first milestone is **one usable
current toolchain**: default global grants, correct callee/callback handling and
bounded prover memory must all pass before adoption. The second is **real client
acceptance**: Studio uses the repaired engine and Character Course ships from a
fresh qualified build. The third is **repeatability**: hosted CI and a fresh public-API
project reproduce that result.

| Priority | Highest-return outcome | Why now |
|---|---|---|
| P0 | Repair and qualify the current compiler tuple | Shared prerequisite for engine proofs, Studio and refreshed packages; three demonstrated failures have concrete controls. |
| P0 | Close actual Studio redraw/reveal and renderer lifecycle failures | Exercises existing implementations in real clients and resolves observed release failures. |
| P1 | Ship the fresh Character Course package, then automate the qualified tuple | Converts repairs into a usable deliverable and prevents repeated manual qualification. |
| P1 | Package a second ordinary application and complete its playable loop | Exposes reusable API and authoring gaps that should select the next subsystem work. |

### Ordered release work

| Order | Concrete deliverable and return | Acceptance / stop condition |
|---|---|---|
| 1 | **Finish compiler grant qualification — Q01/Q03.** This user-requested default policy affects engine and mocap callers. Repair the demonstrated default-driver, exact-callee and callback defects in the owning compiler; qualify its source/runtime pair, then repair demonstrated missing engine caller grants. Use newly published compiler performance changes only after semantic qualification. | Run `scripts/qualify_global_grants.py` on the actual candidate CLI (no opt-in headers); default reads/writes without grants fail; exact grants and transitive caller grants pass; `-permissive` bypasses these checks. Preserve method, callback, default-argument and local-shadowing controls. Record source/runtime/product hashes and complete runtime results. Keep compiler defects in the compiler repository. |
| 2 | **Qualify the repaired prover memory path — Q01.** The previous current pair passes five CLI controls but breaches 3 GiB on the first engine input. The controlled comparison isolates emitted-code behavior. Carry the completed builtin-worklist and custom-pop repairs into the qualified compiler tuple; proceed to original-input acceptance with lexical scope and global-publication controls retained. This shares the toolchain adoption gate with item 1 and prevents wasting the census on a known early failure. | A causal source repair passes the original first input under the unchanged cap, all five original CLI regressions, and all 73 engine reports retaining the original 4,246 obligations plus new obligations. Then run immutable snapshot census/matrix checks with explicit concurrency and Python 3.14; classify all nine historical timeouts. Full shared/native qualification precedes promotion. |
| 3 | **Close actual Studio consumer acceptance.** The mesh repair and build/cook safeguards are integrated at `dc180e49`; consume that tuple and register the qualified read-only observer. This protects a real public API from malformed mutable arrays and has a bounded acceptance workload. | Local source/proof/runtime acceptance passes; consume the clean tuple in the actual Studio redraw path. Retain all original engine proof obligations. O0/O2 controls reject truncated/extra inverse binds, influence shape mismatches, positive-weight joints out of range, incomplete triangles and invalid indices before changing output. Valid geometry remains identical. Add sanitizer evidence where practical, record actual redraw and reveal outcomes against the integrated source/header tuple. |
| 4 | **Resolve renderer lifecycle footprint — R17/Q01.** Use the full-sequence failure and existing heap/GPU/VM diagnostics to identify retained resources or the measured memory domain responsible for the jump. This is the remaining observed native release failure. | A source repair has a reproducer or a decisive resource-accounting regression, then passes the original full renderer lifecycle and native gate. Keep warmup, cycles and the 8 MiB allowance unchanged. A passing retry is supporting evidence, not a diagnosis. |
| 5 | **Refresh and relocate Character Course — Q02/Q04.** Rebuild optimized clean source with the already refreshed and generator-checked cooked content; package the actual public-API client. | Generated outputs match; resource hashes and notices are complete; relocated offline startup, restart and teardown pass with source and Homebrew denied. Keep signing, legal and separate-machine acceptance explicitly open. |
| 6 | **Qualify hosted clean-checkout CI — Q03.** Once the compatible compiler/core/prover/ElisaScript tuple is established, update full-SHA pins and exercise fail-closed provisioning. | An actual hosted headless run retains provisioning, build, proof and package artifacts. A workflow file or local preflight does not satisfy this gate. GPU qualification remains separate. |
| 7 | **Rehearse the ordinary project path — Q07a/Q02.** Follow existing build/cook/package instructions in a fresh project and fix the first demonstrated authoring, resource-discovery or diagnostic gap. | A runnable packaged Elisa application uses public APIs without sample-specific native exports or undocumented steps. Missing/invalid resources yield actionable errors; retain the exact command/product tuple. |
| 8 | **Finish gameplay and physical acceptance — Q07a/Q06.** Extend the existing course route for unsampled win/fall, input, audio and Jolt/GPU ownership outcomes. | Evidence covers the complete playable loop and reload/restart/teardown resource baselines. Physical checks require actual hardware evidence; unavailable hardware does not block ready local tasks. |

### Ordinary-project preparation

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
cache records; failed rollback retains recovery backups. The actual optimized
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
The full fresh public-API author/cook/package rehearsal remains open.

### Bounded consumer work while release gates run

Mocap-cleaner requested the read-only
`elisa_studio_build_trash_observe_locations_v1` observer. Native implementation and
focused identity, package-layout, path replacement, readonly-operation and uncertain
publication controls are qualified in engine-mocap `3f60d5ed`. The clean source and
public-header tuple has been handed to the consumer owner. **The next valuable
slice is provider registration and actual Studio reveal acceptance.**

Register the three observer translation units, consume the bounded version-2 wire
format, match the full retained binding and reobserve immediately before reveal.
Keep uncertain paths empty. Exact directory/lease identity does not authenticate
product contents or authorize restore or deletion. Acceptance requires the actual
Studio consumer with original, quarantine, conflicting occupant and uncertain
location outcomes. See the [coordinated M track](docs/plans/mocap-engine-track.md).

### Immediate source slices and handoffs

1. **Compiler owner → engine:** qualify default grant rejection, including nested
   imports, methods and callbacks, plus the explicit permissive bypass. Consume
   the clean compiler/runtime tuple, then migrate only demonstrated engine caller
   chains. A partial compiler test run does not authorize installing that product.
2. **Engine owner → mocap consumer:** use clean integrated dependency `dc180e49` (mesh repair retained) for
   actual Studio redraw and observer registration/reveal acceptance. Local mesh
   source acceptance passes; avoid another handoff-only slice or dependency move
   while the consumer compiler repair is in progress.
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
5. **Mocap integration owner:** register and consume the qualified observer's
   clean source/header tuple, then exercise actual Studio reveal acceptance.
   Read-only observation must not become restore or deletion authority.

Hold heavy census/native runs while the coordinated compiler semantic preflight
is live. Exact callee, callback and binding-scope repairs require qualification
before actual consumer acceptance. Start another compile only after an explicit
terminal slot handoff; a process snapshot does not establish availability. The
interrupted snapshot census is incomplete evidence.

### Start next: one ready task, one decisive result

| Readiness | Task | Why it leads / next action |
|---|---|---|
| Backend repair complete; qualified integrated compiler required | Qualify repaired prover memory on original engine input | Integrate the qualified default-grant compiler repair with backend e791ec50 and consume the latest clean prover source (including signed-minimum and source-annotation repairs), then build an authenticated pair and repeat the unchanged original input under 3 GiB. Follow with five CLI regressions and all 73 engine reports; diagnostic harness success does not close those gates. |
| Owning compiler repair in progress | Default grants and Studio compiler repair | Highest cross-client return. Require 22/22 actual CLI controls, exact-callee/callback regressions and authenticated source/runtime/product identity before client acceptance or installation. |
| Consumer integration ready; execution depends on compiler | Actual Studio redraw and observer reveal | Use `dc180e49` and the retained observer header identity. Validate the existing public API in its real consumer; avoid further handoff-only work. |
| Ready without a heavy compile slot | Finish native executable/provenance publication — Q07/Q02 | Prepare the manifest from the staged binary using the final output identity; publish it with runtime links before the executable. On preparation or replacement failure, retain the previous executable, links and sidecar; retain recovery data if rollback fails. Use focused Python fault controls. Focused preparation, replacement, rollback and identity controls pass; actual optimized package acceptance remains open. |
| Ready after coordinated heavy slot | Renderer lifecycle diagnosis and repair | Observe the failing full sequence, identify the retained owner/domain, then repair its lifetime. Keep original cycles, warmup and 8 MiB allowance. |
| Ready after compatible tuple and native gate | Fresh package and ordinary author workflow | Reuse one clean optimized build for Character Course relocation and a fresh public-API application. Console image cooking and native package reading are preparation; the packaged application remains open. |

When the completed compiler product arrives, qualification takes precedence.
While waiting, integrate the completed native provenance transaction and
select a demonstrated source failure with an independent acceptance path.
Reserve each heavy slot through the existing coordination; record its
explicit handoff and terminal result in the validation record, since slot ownership
changes during execution. Defer census, broad native retries and package rebuilds
until their prerequisite can produce a meaningful acceptance result.

### Next three outcomes

1. **Integrate the completed publication transaction.** Native builds now prepare
   provenance before publication and roll back sidecar/link changes on failure;
   48 build/provenance controls pass. Carry this source into the fresh optimized
   package rehearsal once the toolchain gate closes.
2. **Obtain one qualified integrated compiler/runtime tuple.** Resolve the lock
   candidate defect, pass default grant/refusal and permissive controls, and retain
   the backend ownership repairs. Install only the authenticated passing product.
3. **Spend that tuple on real acceptance.** Run the original prover input first;
   use separately reserved slots for actual Studio redraw/reveal and the failing
   renderer lifecycle sequence. Their results select the next source repair.

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
