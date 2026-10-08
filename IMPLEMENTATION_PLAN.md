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
- **Compiler policy — default grants and integrated app build qualified; consumer run remains open:**
  compiler commit `ee028bbb` enforces `Global.Read` / `Global.Write` for mutable
  globals by default, with explicit `-permissive` bypass. Its exact Stage1 product
  passes 52 grant/global cases and six explicit permissive bypass cases, 12 grouped
  protocol controls, eight runtime wrapper/grant controls, and bounded rehome
  checks at O0/O2 including the nested callback reproducer. The follow-up export
  target fix is `dc4a7486`; compiler source and product provenance are recorded in
  [grant qualification](docs/validation/global-mutable-grants.md). Compiler
  source `2a3dce66` also rejects by-value optional-to-payload-reference coercion
  while preserving `T?&` container borrowing; its negative/positive verifier pair
  and the 52 grant cases, six permissive bypasses, eight runtime controls, 12
  grouped-protocol controls, strict unsafe and export controls pass. Studio callers
  bind optional payloads and preserve absence behavior. A previous sealed snapshot
  exposed that the generation inventory omitted `semantic.log` and `compiler.log`;
  root fix `7d47b063` closes that issue. The exact compiler/UI/engine tuple
  builds, verifies, links and seals `build/MocapStudio.app`; the bundle records
  project base `bca72d19`, engine `4b0a9af7`, UI `261363eb`, compiler
  `2a3dce66`, dirty project/UI worktrees, and the sealed input/executable
  identity. The 2026-10-08 consumer preflight now passes against current source,
  frontend, compiler and runtime. Strict grant checks found missing
  `Global.Read` / `Global.Write` declarations in legacy native test entrypoints
  and the CLI call graph. Source contracts are now repaired across the shared
  audio/input/decision/UI helpers and the Character Course call graph; its
  hidden self-test application builds and links with default enforcement on
  compiler source `dd8aea22` (Stage1 SHA256
  `a95da6ad1daee9aab219047782ec04ca5ecb3d063f653f9a2e06b3cdf2a626a3`). The
  complete consumer check still needs a sequential rerun: overlapping runs
  raced in shared test/proof outputs, so their reports are not qualification
  evidence. Despite the
  file-picker timeout, Studio loaded the
  FBX as a 337-frame take. The skinned surface is available and `M` switches
  between Character and Skeleton. Playback is the remaining blocker: the GLB has
  sparse two-key channels mixed with 337-frame channels, while Studio requires
  aligned sample times. Fix the grid at the import boundary and verify playback.
  The five engine source files in this checkout now declare explicit native
  effect contracts; the maze C-ABI example and viewport-gizmo test also declare
  their `Global.Read` / `Global.Write` grants and pass the current Stage1
  target checks ([evidence](docs/validation/global-mutable-grants.md)). The
  build runner now passes an empty runtime-object path to direct compiler
  invocations, matching the wrapper's `none` translation and preventing a
  bogus `none` archive member. Keep these contracts with their functional
  source slice. Preserve exact
  callee, callback, default-argument, shadowing, and profiler/host-callback
  checks.
  **FBX isolation:** current native staging/conversion passes for source SHA
  `50048a8a…`, emits GLB SHA `36cba16f…`, and preserves the original. Earlier
  direct worker loading passed, but task/readiness/join crashed at
  `arena_alloc +356`. The selected compiler includes the global pose-cache
  rehome fix, and the integrated app now imports and switches the skinned mesh.
  Keep playback acceptance open until converter output aligns every animated
  channel with the 337-frame timeline and the pose visibly advances.
- **Shipping client:** the guide rig refresh is committed at `a30d3af9`.
  Current rig, sound and cell generator checks pass on this checkout. The
  historical relocated Character Course bundle still needs a fresh optimized
  build/package acceptance; older green runs do not qualify the replacement
  toolchain or package.

### Delivery milestones and ranking

Use dependencies and expected unblock value to choose the next task; table order
is not a reason to idle behind another owner. The first milestone is **real
client acceptance**: run the committed sealed Studio build through FBX import,
redraw and observer reveal, and qualify the current-source prover against its
original memory budget. The second is **repeatable shipping**: diagnose the
measured renderer lifecycle failure and ship Character Course from a fresh
optimized build. The third is **reuse**: a second ordinary public-API project
and hosted clean-checkout CI reproduce the qualified tuple.

| Priority | Highest-return outcome | Why now |
|---|---|---|
| P0 | Finish committed-tree Studio FBX/redraw/reveal acceptance | The authenticated compiler/engine/UI tuple now seals; one real workflow closes the highest-value consumer gate and catches integration defects. |
| P0 | Qualify the current-source prover under its original budget | A causal compiler allocation repair passes the diagnostic harness; only the original input and full engine inventory establish compatibility. |
| P0 | Diagnose the measured renderer lifecycle memory failure | This is an observed release failure with focused diagnostics; establish the retaining owner before changing resource lifetimes. |
| P1 | Ship the fresh Character Course package, then automate the qualified tuple | Converts repairs into a usable deliverable and prevents repeated manual qualification. |
| P1 | Package a second ordinary application and complete its playable loop | Exposes reusable API and authoring gaps that should select the next subsystem work. |

### Ordered release work

| Order | Concrete deliverable and return | Acceptance / stop condition |
|---|---|---|
| 1 | **Complete Studio consumer acceptance — Q01/Q03.** The sealed bundle records project base `bca72d19`, engine `4b0a9af7`, UI `261363eb` and compiler `2a3dce66`; project/UI worktrees are marked dirty. The target FBX stages/converts, loads as a 337-frame take, exposes its skinned surface, and switches with `M`. Playback fails because two-key animation channels are mixed with 337-frame channels. | Resample animated channels onto a common import grid, prove the pose advances in the actual app, and retain constant-channel behavior. Then cover malformed input and observer reveal with the exact sealed snapshot. |
| 2 | **Qualify the repaired prover memory path — Q01.** Current-source preflight passes on the authenticated compiler/runtime pair. The diagnosed grant omissions now have source contracts, and the Character Course hidden self-test application builds and links under default enforcement. Rerun the complete consumer check sequentially because overlapping runs raced in shared outputs. Apply the builtin-worklist and custom-pop repairs whose 25-obligation diagnostic harness fell from 757,284,864 to 16,089,088 bytes. That is diagnostic evidence only; original-input and full-inventory acceptance remain open. | The current clean pair passes the original first input under 3 GiB / 120 seconds with all 265 obligations proved and independently replayed, then the five CLI regressions and all 73 engine reports. Preserve the original 4,246 obligations, run immutable snapshot census/matrix afterward, and classify all nine historical timeouts. |
| 3 | **Finish actual redraw/reveal and observer integration.** Consume the qualified mesh repair and read-only observer in the real Studio path. This closes a public consumer contract and confirms the native guards through the app. | Studio redraws valid geometry unchanged; malformed inverse-bind, influence, joint and triangle shapes fail before output changes. Observer registration/reveal passes for original, quarantined, conflicting and uncertain locations without gaining restore/delete authority. |
| 4 | **Resolve renderer lifecycle footprint — R17/Q01.** Use the full-sequence failure and heap/GPU/VM diagnostics to identify the retaining owner and measured memory domain. | A source fix has a reproducer or decisive resource-accounting regression, then passes the original full lifecycle/native gate with warmup, cycles and the 8 MiB allowance unchanged. A retry alone is not a diagnosis. |
| 5 | **Refresh and relocate Character Course — Q02/Q04.** Rebuild optimized clean source with refreshed, generator-checked content and package the public-API client. | Generated outputs match; resource hashes/notices are complete; relocated offline startup, restart and teardown pass with source/Homebrew denied. Keep signing, legal and separate-machine acceptance open. |
| 6 | **Rehearse a second ordinary project — Q07a/Q02.** Follow current build/cook/package instructions in a fresh project and repair the first demonstrated blocker. | A runnable package uses public APIs without sample-specific native exports or undocumented steps; invalid resources produce actionable errors. Record exact command and tuple. |
| 7 | **Qualify hosted clean-checkout CI — Q03.** Promote the qualified local compiler/core/prover/ElisaScript tuple to full-SHA pins and fail-closed provisioning. | A hosted headless run retains provisioning, build, proof and package artifacts; GPU qualification stays separate. |
| 8 | **Finish gameplay and physical acceptance — Q07a/Q06.** Extend the course route for unsampled win/fall, input, audio and Jolt/GPU ownership outcomes. | The complete playable loop and reload/restart/teardown baselines pass. Physical checks require hardware evidence; unavailable hardware does not block ready local work. |

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

1. **Studio integration:** compiler source `2a3dce66`, its fresh product/runtime,
   linked engine source `4b0a9af7`, the UI caller repair and root inventory fix
   `7d47b063` produced a sealed `build/MocapStudio.app`. The consumer owner is
   running the bundle from its sealed input snapshot; metadata marks its project
   and UI worktrees dirty. Finish FBX acceptance and repeat from clean inputs
   before calling the build reproducible. Migrate only caller chains or FFI
   effect boundaries exposed by that run.
2. **Engine owner → mocap consumer:** use clean integrated dependency `dc180e49` (mesh repair retained) for
   actual Studio redraw and observer registration/reveal acceptance. Local mesh
   source acceptance passes; avoid another handoff-only slice or dependency move
   while sealed-bundle FBX acceptance is in progress.
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

Hold heavy census/native runs while the coordinated live app checks are running
or compiler inputs are changing. Exact callee, callback and binding-scope repairs
require qualification before consumer acceptance. Start another compile only
after an explicit terminal slot handoff; a process snapshot does not establish
availability. The interrupted snapshot census is incomplete evidence.

### Start next: one ready task, one decisive result

| Readiness | Task | Why it leads / next action |
|---|---|---|
| Sealed app snapshot; FBX load and Character/Skeleton switch pass | Align FBX channels to the timeline grid | Fix sparse two-key channels at import, verify pose advances across the 337-frame timeline, then cover malformed input and observer reveal. |
| Backend repair complete; current-source prover pair required | Qualify repaired prover memory on original engine input | Integrate default-grant compiler with backend `e791ec50` and latest clean prover repairs, then run unchanged first input under 3 GiB / 120 seconds. Follow with five CLI regressions and all 73 engine reports; diagnostic-harness success does not close these gates. |
| Consumer integration ready | Actual Studio mesh redraw and observer reveal | Use the current mesh repair and retained observer-header identity; validate the public API in the real consumer. |
| Ready without a heavy compile slot | Reproduce a concrete ordinary-project obstacle — Q07/Q02 | Publication rollback, manifest identity and package resource controls are implemented and registered in the unit stage. Walk the existing author workflow and implement only a newly demonstrated failure that prevents a runnable public-API package. End the slice when that failure is repaired; full native and hosted unit-stage execution remain open. |
| Ready after coordinated heavy slot | Renderer lifecycle diagnosis and repair | Observe the failing full sequence, identify the retained owner/domain, then repair its lifetime. Keep original cycles, warmup and 8 MiB allowance. |
| Ready after compatible tuple and native gate | Fresh package and ordinary author workflow | Reuse one clean optimized build for Character Course relocation and a fresh public-API application. Console image cooking and native package reading are preparation; the packaged application remains open. |

While Studio and prover acceptance run, select source work with an independent
acceptance path. Completed publication safeguards need actual package consumption;
additional fault controls alone do not outrank release acceptance.
Reserve each heavy slot through the existing coordination; record its
explicit handoff and terminal result in the validation record, since slot ownership
changes during execution. Defer census, broad native retries and package rebuilds
until their prerequisite can produce a meaningful acceptance result.

### Current compiler and consumer status

Compiler source `2a3dce665ff462096de961d340a50cc1ef83369d` has a fresh,
source-matched Stage1/runtime pair. It passes 52 grant/global cases, six explicit
permissive bypasses, eight runtime controls, 12 grouped-protocol controls, strict
unsafe enforcement, export alias/ABI controls, and the optional/reference
negative/positive LLVM-verifier regression. Exact product and provenance hashes
are in [global grant validation](docs/validation/global-mutable-grants.md). The
Studio caller repair, root inventory fix `7d47b063` and linked engine source
`4b0a9af7` produced a sealed app bundle. Its metadata records base project
`bca72d19`, UI `261363eb`, compiler `2a3dce66`, and dirty project/UI worktrees;
its seal verifies the recorded inputs and copied executable.
`mocap-cleaner/scripts/check.sh` reports that the prover frontend/compiler
manifest is stale relative to current Stage1 and needs a rebuilt, qualified
pair. The app imports the source as a 337-frame take and `M` switches its
skinned surface; playback is blocked by sparse key grids. The consumer owner is
fixing converter sampling. Full prover, mesh redraw/reveal,
renderer, package and native release gates remain separate acceptance items.
The world epoch, catalog-brand and access-frame identity counters now also
declare `Global.Read` / `Global.Write`; their three caller tests compile and run
on default-enforcement Stage1 `0b43cbdb` (artifacts and hashes are recorded in
[grant qualification](docs/validation/global-mutable-grants.md)). This is a
bounded source-adoption result, not full-engine grant qualification.
Core world scheduling and save-swap APIs now propagate the same grants; six
world command/event/iteration/save tests compile and run on that product. Their
source and binary hashes are captured in the linked validation record. Continue
grant adoption along shared engine APIs with their real callers; keep the broad
native-wrapper audit out of bulk migration until each boundary's effect contract
is understood. The application, input, and UserData native boundaries have now
been audited individually: explicit `Unsafe.RawExtern` contracts and narrow
trusted call sites remove the false Elisa-global effects without making the
public APIs unsafe. The full UserData consumer probe now type-checks with no
grant diagnostics, and the application/input path passes strict-unsafe object
compilation. The UserData wrapper also passes the new compiler's strict
never-leak scope lint after temporary FFI state was localized and the staged
payload result was expressed as a tuple block. Record this as a high-yield,
bounded source-adoption win; continue with newly demonstrated roots alongside
their real callers. Evidence and limits are in
[global grant validation](docs/validation/global-mutable-grants.md).

### Next three outcomes

1. **Finish FBX animation acceptance.** Resample sparse channels onto the
   common timeline grid, verify the 337-frame pose advances in the sealed app,
   then complete malformed-input and read-only observer reveal checks.
2. **Refresh proof provenance and close the original prover memory gate.**
   Resolve the current strict-grant failures in legacy consumer tests and the CLI,
   then rerun the consumer check without overlapping writers. Prove/replay all
   265 obligations of the original first engine input under 3 GiB / 120 seconds,
   then advance to the five CLI controls and all 73 reports. Stop at the first
   actionable failure and repair its owner.
3. **Turn the qualified tuple into repeatable shipped clients.** Diagnose the
   renderer lifecycle failure, reuse a fresh optimized build for Character
   Course relocation and a second ordinary public-API application, then register
   the tuple in hosted CI after local acceptance passes.

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
