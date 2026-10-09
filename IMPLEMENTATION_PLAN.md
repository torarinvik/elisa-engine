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

The detailed compiler/runtime evidence, exact source and binary identities, and open-gate status are maintained in [IMPLEMENTATION_EVIDENCE.md](IMPLEMENTATION_EVIDENCE.md). The execution order and acceptance criteria follow below.

### Delivery milestones and ranking

Use dependencies and expected unblock value to choose the next task; table order
is not a reason to idle behind another owner. The immediate milestone is the
reproduced function-value aggregate ABI failure: establish a fresh compiler
chain, pass the focused callback regression, then retry the original FBX import.
Next, refresh the exact default-grant compiler/runtime and proof pair. Then
complete Studio's full import, playback, redraw, malformed-input and observer
acceptance on that tuple. Close the measured prover-memory and renderer-lifecycle
gates next; then refresh the shipping package, exercise a second ordinary
public-API project, and promote the qualified tuple to hosted clean-checkout CI.

| Priority | Highest-return outcome | Why now |
|---|---|---|
| P0 | Repair function-value callback region ABI and ownership | The fresh sealed O0 crash is a null arena dereference in `arena_alloc`; `StudioFbxImportWorker.path_copy` receives its hidden region in x1, but the generic function-value callback does not forward it. The initial 1,024-byte aggregate fix `37091274` remains necessary but did not close this failure. Compiler branch commit `d5a9b58a` adds callback region metadata and worker result-arena transfer; four core-file conflicts are being reconciled against the newer compiler. No combined Stage1/runtime or Studio retry is qualified yet. See [evidence](IMPLEMENTATION_EVIDENCE.md). |
| P0 | Integrate default global grants into the proof and consumers | Combined compiler source `12120f6b` has a fresh pair: Stage1 `356d4a14…`, runtime `013d3174…`. Strict driver checking, 103 authority cases, 24 actual CLI controls (including `-permissive`), Stage0/Stage1 parity, nested match-arm and large-aggregate regressions pass; gen3 fixpoint and 40-run reproducibility pass. This exact pair exposed 49 missing local grants in `examples/maze/capi.elisa` and 12 in `test/viewport_gizmo.elisa`. Narrow body scopes now pass strict checks; the embedded maze host and viewport executable assertions also pass. Refresh the proof migration's pre-`trusted` inventory on this pair before exact build/replay qualification. |
| P0 | Build and exercise the exact Studio consumer tuple | The shared-grid converter bridge passes focused strict controls. The O2 build reached LLVM AArch64 DAGCombiner at about 28.4 GB physical memory and stopped without an object. The owner added `STUDIO_OPT_LEVEL` (O2 stays default) and built/sealed an O0 app against compiler SHA `ad0e16c9…` and runtime `013d3174…`; it still quits when opening the FBX. The new report confirms a null hidden arena at `arena_alloc`; the callback ABI repair is not yet integrated or qualified. Once it is, rebuild against the exact compiler/runtime and verify all 337 frames, repeated redraw, malformed-input refusal and observer reveal. |
| P0 | Qualify the repaired prover under its original memory budget | The builtin-worklist/custom-pop repair has strong focused allocation evidence, but the original first input and full 73-report inventory must pass under the unchanged 3 GiB / 120-second limit. |
| P0 | Diagnose the measured renderer lifecycle memory failure | This is an observed release failure with focused diagnostics; identify the retaining owner before changing resource lifetimes, then preserve the original 8 MiB gate. |
| P1 | Restore the source-length gate — complete in `9e40976a` | Split asset-cook validation and macOS launcher generation into focused modules, separated launcher tests, and included the new configuration module in cook-cache identity. The full 600-line gate and 91 focused tests pass. See [validation](docs/validation/source-length-policy.md). |
| P1 | Ship a fresh Character Course package and automate the qualified tuple | Converts the repaired path into a usable deliverable, then prevents repeated manual qualification. |
| P1 | Package a second ordinary application and complete its playable loop | Exposes reusable API and authoring gaps that should select the next subsystem work. |

### Ordered release work

| Order | Concrete deliverable and return | Acceptance / stop condition |
|---|---|---|
| 1 | **Repair function-value callback region ABI and ownership — Q01/Q03.** The fresh O0 crash report faults in `arena_alloc` at `+0x58` while dereferencing a null arena; disassembly shows `path_copy` expects its hidden region in x1, which the generic callback path does not forward. Commit `d5a9b58a` adds callback region metadata and task-owned result arena transfer, but integration with the newer compiler has four core-file conflicts. | Reconcile the ABI repair with current compiler source, add/retain regression coverage for aggregate arguments, hidden region slots and result lifetime, build a fresh exact Stage1/runtime, then retry the same FBX in the sealed O0 app. O2 remains blocked after reaching AArch64 DAGCombiner at about 28.4 GB without an object. |
| 2 | **Integrate compiler default grants — Q01.** Combined source `12120f6b` is freshly seeded and passes strict compiler-driver checking, 103 authority cases, 24 actual CLI controls, permission-row parity, gen3 fixpoint/reproducibility, the nested permission-match regression and the large-aggregate smoke. The engine C ABI and viewport test have local body grants, pass strict checks, and pass the embedded-host and viewport executable regressions on this pair. The proof worktree's 1,166 earlier findings and `check_full_into` grants predate corrected `trusted` behavior. | Refresh the proof diagnostic inventory on this exact Stage1/runtime pair, build and replay the current proof product, qualify the original 265 obligations and five CLI regressions, then retain all 73 engine reports. Rebuild Studio with that same compiler and address only reproduced backend declines. |
| 3 | **Complete current-tuple Studio consumer acceptance — Q01/Q03.** Consumer commit `1e98f075` aligns multi-key FBX tracks. Its bounded bridge regression passes on the promoted compiler's fresh Stage1/runtime pair against the supplied high-block FBX, preserving source bytes and aligning multi-key clocks ([evidence](docs/validation/global-mutable-grants.md#promoted-exact-callee-compiler-and-fbx-bridge-check)). This proves conversion structure, not fresh-app playback. | Build the app from the exact current compiler/engine/UI tuple; verify pose advancement through all 337 frames, constant channels, repeated redraw, malformed-input refusal and observer reveal in that same sealed snapshot. |
| 4 | **Qualify the repaired prover memory path — Q01.** Current-source preflight passes on the authenticated compiler/runtime pair. The diagnosed grant omissions now have source contracts, and the Character Course hidden self-test application previously built and linked under default enforcement. Rerun the complete consumer check sequentially because overlapping runs raced in shared outputs. Apply the builtin-worklist and custom-pop repairs whose 25-obligation diagnostic harness fell from 757,284,864 to 16,089,088 bytes. That is diagnostic evidence only; original-input and full-inventory acceptance remain open. | The current clean pair passes the original first input under 3 GiB / 120 seconds with all 265 obligations proved and independently replayed, then the five CLI regressions and all 73 engine reports. Preserve the original 4,246 obligations, run immutable snapshot census/matrix afterward, and classify all nine historical timeouts. |
| 5 | **Finish actual redraw/reveal and observer integration.** Consume the qualified mesh repair and read-only observer in the real Studio path. This closes a public consumer contract and confirms the native guards through the app. | Studio redraws valid geometry unchanged; malformed inverse-bind, influence, joint and triangle shapes fail before output changes. Observer registration/reveal passes for original, quarantined, conflicting and uncertain locations without gaining restore/delete authority. |
| 6 | **Resolve renderer lifecycle footprint — R17/Q01.** Use the full-sequence failure and heap/GPU/VM diagnostics to identify the retaining owner and measured memory domain. | A source fix has a reproducer or decisive resource-accounting regression, then passes the original full lifecycle/native gate with warmup, cycles and the 8 MiB allowance unchanged. A retry alone is not a diagnosis. |
| 7 | **Restore the source-length gate — complete in `9e40976a`.** Split the three over-limit Python files by responsibility and fingerprint the extracted asset-cook configuration module. | `python3 scripts/check_source_length.py` passes; 41 packaging, 48 build/run and asset-cook, and 2 dense-cook tests pass. Full details and log paths are in [validation](docs/validation/source-length-policy.md). |
| 8 | **Refresh and relocate Character Course — Q02/Q04.** The clean current-source optimized public entrypoint builds and packages against the default-grant compiler. Complete relocation acceptance after the grant checker is corrected. | Generated outputs match; resource hashes/notices are complete; relocated offline startup, restart and teardown pass with source/Homebrew denied. Keep signing, legal and separate-machine acceptance open. |
| 9 | **Rehearse a second ordinary project — Q07a/Q02.** Follow current build/cook/package instructions in a fresh project and repair the first demonstrated blocker. | A runnable package uses public APIs without sample-specific native exports or undocumented steps; invalid resources produce actionable errors. Record exact command and tuple. |
| 10 | **Qualify hosted clean-checkout CI — Q03.** Promote the qualified local compiler/core/prover/ElisaScript tuple to full-SHA pins and fail-closed provisioning. | A hosted headless run retains provisioning, build, proof and package artifacts; GPU qualification stays separate. |
| 11 | **Finish gameplay and physical acceptance — Q07a/Q06.** Extend the course route for unsampled win/fall, input, audio and Jolt/GPU ownership outcomes. | The complete playable loop and reload/restart/teardown baselines pass. Physical checks require hardware evidence; unavailable hardware does not block ready local work. |

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

1. **Studio integration:** the sealed O0 diagnostic app opens its file picker,
   then crashes importing the supplied high-block FBX because a 9,232-byte
   `Job` crosses a function-value callback with the wrong aggregate ABI. The O2
   attempt on compiler `9d2cf1b` stopped in AArch64 SelectionDAG/DAGCombiner
   without an object. Neither build qualifies Studio. Rebuild the app only after
   the compiler ABI repair passes its source-matched bootstrap and callback
   regression; then exercise the bundle from sealed inputs and repeat from clean
   inputs before claiming reproducibility. Migrate only caller chains or FFI
   effect boundaries exposed by that run.
2. **Engine owner → mocap consumer:** after the callback ABI regression passes,
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
| Studio/UI strict semantic gate is clear; six codegen declines and proof freshness remain open | Repair the five generic-helper call declines and AppKit file-drop callback, refresh and authenticate the proof pair, then build the sealed Studio app and run playback/redraw/reveal acceptance. After explicit slot handoff, run `physics_rig` and the latest Studio character regression sequentially. | Mocap source commit `b79608a7` and UI commit `65f370f3` pass strict app semantics. Clean committed compiler snapshot `19294e83` Stage1 SHA is `752fe51db92ed27d3ef133e0f3e94b757e98964727b5702b7e6af02c7925bec5`; runtime SHA is `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. Its detached provenance passes. The shared checkout has uncommitted generic-codegen edits and must be reseeded before they can qualify. The proof frontend pin `2a3dce66` is stale; rebuild the proof snapshot on a matching compiler/runtime pair without weakening freshness. No current Studio app or physics executable is qualified. |
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

Compiler exact-callee authority is promoted on `42fd1cbee38293b1f7c66a9a6b05eab58b3437c6`.
Its candidate worktree has a fresh Stage1/runtime pair (Stage1 SHA256
`599b3f762db05dba698af0818d3af331387ebcbac64be2f1b137cb0811d97133`, runtime
SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`).
The focused authority suite (103 cases), Stage0/Stage1 parity, grouped-effect
formatting, CLI grant behavior and strict-Unsafe controls pass. The broader Core
fast suite still fails because legacy fixtures omit local global grants and
semantic tests expect old warning text. The primary compiler checkout now has
a fresh installed Stage1 (`71df842ce3fef2e456337d0d1b2c8da9082ca43a0a022d098701aee833825615`)
paired with runtime `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`;
Stage2 self-host passed, the installed binary passed the focused grant/format
smokes, and a proof agent has the exact source/product snapshot for Linux-native
qualification.

Compiler commit `891d2d30ac544245de89a0758e78c6720e94d068` contains the
void-return ensure backend repair and `test/parity/void_return_ensure_smoke.sh`.
The current source-matched Stage1 candidate is SHA256
`ef71c5298815fae721a31082faea6002a247a808dea6420af6a744be0203a359`, paired
with runtime SHA256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. The
focused regression passes at O0 and O2. This is not yet full compiler/Core or
consumer qualification. The compiler checkout still has a local JSON standard
library grant edit; the Mocap owner is working through current source/parser and
UI blockers. Do not reuse the older `8110c2c6` candidate for acceptance.

Consumer branch `1e98f075` aligns multi-key FBX animation tracks, and its bounded
bridge regression passes on the fresh `42fd1cbe` product with strict default
grants. The currently installed Studio bundle is stale: it records an older
dirty compiler and predates current consumer source. The earlier temporary
`d73f2cf3` preflight logged 10,017 grant-related diagnostics. The follow-up
preflight at `mocap-cleaner/build/studio-build.5iL67X/semantic.log` was current
for its recorded snapshot at compiler commit `42fd1cbe`, fresh Stage1 SHA
`5f3735d2503f4add196f64ac35af10bdcdbc8a49e1ad5749819642057dfc52ed`, and runtime
SHA `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`. It
reports 2,253 global-grant diagnostics across Mocap Studio/core source and 47 UI
contract diagnostics (31 global grants, 16 Painter protocol capabilities), plus
separate return-flow and runtime type errors. It produced no app. The compiler
checkout then added six `Global.Read/Write` annotations to its JSON standard
library and rebuilt Stage1; the matching product SHA is
`f5f36a9f726355984382a3aa84f63f8653e0e68b9546778877b85c2906bd0ddb`, with the
same runtime SHA. Its provenance check passes against the edited compiler source.
The next preflight at `mocap-cleaner/build/studio-build.eZoCyo/semantic.log`
reports 1,260 Global-grant findings and 34 UI capability rows; completed source
repairs cover generated icons, geometry drawing, pointer and keyboard input,
gizmo handling, app entry points, export review/publication, and batch
accessibility. The follow-up `build/scene-character-check-current.log` reports
zero diagnostics in `scene.elisa` and `character.elisa`, with 1,239 grant
findings remaining in the full closure. Test commit `aa9f018c` adds the grants
needed by `test/studio_character.elisa`; its current executable build clears all
test-file findings but still stops on 191 dependency findings across 19 files.
The largest are `rig_physics.elisa` (36), `rig_legs.elisa` (35), and
`model.elisa` (33). The regression has not executed yet. Separate return-flow
and runtime errors and the fresh-app acceptance remain open. The old installed
app bundle still cannot qualify current playback or the skin-cache fix.
Those counts are from the earlier `eZoCyo` / scene-character snapshot and are
stale after subsequent consumer commits. The focused physics source/test change
was integrated into Mocap main at commit `1c3dea3b` (original isolated patch
`3da1c5ba`). It adds local scopes across the physics regression's direct
dependency closure and pairs them with `test/physics_rig.elisa`. Current Stage1
semantic checking passes that complete closure with no grant diagnostics. Executable
emission is blocked by three backend declines: `balance_frames@12`,
`accumulate_residual@11`, and `merge_residual@39` return statements. The same
three declines reproduce on parent `ecc743c6` with `-permissive`, before these
scopes, so this is an independent compiler/code-generation blocker. The test
binary was not emitted and runtime behavior remains unqualified. Rerun the
Studio dependency census on the latest integrated source before using old totals.
The generated Studio icon function now opens the required local grant scope;
the regenerated 30-icon / 383-segment output passes the standalone icon test on
the installed Stage1. Consumer commit `dfe41779` carries the generator and test
source together. This removes the 383 generated-source errors from the latest
preflight, not the broader UI/Studio grant migration.
The full current Character Course census, actual app playback/repeated redraw,
mesh redraw/reveal, prover qualification, renderer diagnosis and shipping gates
remain open.
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

### Latest integrated checkpoint — 2026-10-08

This checkpoint supersedes earlier consumer diagnostic counts and candidate
hashes in the historical notes above. The consumer owner reports a clean strict
Stage1 semantic check after Mocap commit `b79608a7` and UI commit `65f370f3`,
using compiler source `34574304`. The current Stage1 product SHA256 is
`f4c9d54c759f098b294c2146c28c372fa465da6e4cb7f2b48135cbe1e0f189c4`.
`mocap-cleaner/scripts/build_studio.sh` initially stopped at its proof freshness
gate because the proof checkout pinned frontend `2a3dce66` while Stage1 came from
`34574304`. The subsequent proof build advanced to current compiler sources but
stopped on 45 missing default-global grants (39 in `src/semantic/symbols.elisa`,
five in `src/semantic/symbols_hash_index.elisa`, one callback in
`src/parser/parser_machine_states.elisa`). A documented runtime-checks fallback
then declined two backend declarations, so no proof-qualified prover is
available. The latest build-only app attempt initially declined 25 constructs.
The focused runtime `join` change and scalar/32-byte aggregate regression now
pass at O0 and O2 against a fresh seed; the current app compile declines 17
constructs. Remaining diagnostics include `ctx_concurrency_result_read` and its
index-expression specialization, `generation_scan_root`, five generic call
expressions and the AppKit file-drop callback. Its Stage1 product hash is not
yet recorded. The saved crash trace maps to an older retained package
(`studio-package.cO7zyV/previous.app`, project `31459ca3`, compiler `e24c29e6`);
it does not qualify or contradict the current source. The repository executable
UUID `A20800ED…` is also from a separate older build, and no current app bundle
is qualified. Do not suppress the grants or weaken provenance. After fresh
proof acceptance and backend support, build and seal the app, then run the
existing FBX playback, redraw, malformed-input and observer reveal checks. The
physics executable and renderer-memory diagnostic remain queued behind explicit
compiler/native slot handoffs. The existing renderer
diagnostic preparation records engine commit `ebf55d46`, while the current engine
head is `d56ed6cc`; revalidate and regenerate its source/dependency hashes before
using it.

### Compiler/toolchain checkpoint — 2026-10-09

The latest provenance-checked compiler product is from clean main source
`ec41ca95`: Stage1 SHA256
`acc5c27caf0fae47b39d0bba0b870b5d29f218c18f50130400b7e462bf01c526`, runtime
SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`,
and source-tree hash
`391012beba8144c36eb131598f94db5d80bfc9069643c3dec368025c86c11e44`. The
corrected actual-CLI mutable-global grant gate passes 24/24 controls on this
exact product, including the explicit `-permissive` bypass. This qualifies
compiler source admission only; the proof pair and current Studio app remain
unqualified. The prior `19294e83` product still records the 103-case authority
smoke, Stage0/Stage1 permission parity and SDK package SHA256
`d9664cd42f15cfafadd13bfbe5d110c3c24a6070860b18e27b25d3f4edda1d6e` as
historical evidence.

Compiler main includes fixed-array `usize` inference (`ca06f6cb`) and effectful
export-alias resolution (`ec41ca95`); the clean main product above passes
provenance. The isolated `codex/compiler-global-grant-adoption` branch is also
based on `ec41ca95` and has localized grants across 93 compiler source files.
Its first full strict check on pre-scope Stage1 `358a2d3b` ran over four
hours and was stopped in the generic effect-row path. A five-second sample
placed 2,915 of 3,203 main-thread samples in `ga_generic_call_rows`, repeatedly
scanning all annotations to classify generic permission parameters. The source
now indexes parameter names once and uses the existing indexed predicate. After
the seed exposed 29 bindings that escaped narrow `can` scopes, outer mutable
locals now receive the allocated results inside those scopes. A fresh branch
Stage1 seed passed from the clean Stage0 oracle: product SHA-256
`a1fc208ff1b68a6025b7d2360294bb26d1d944de092dc38152ac5fedc9a1a5a6`, source tree
SHA-256 `bd8d8d67cb612f4a335a1d7438a0e4fa9f8a6966d85e576383cf0e2a33029883`, and
matching runtime SHA-256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
The exact product's strict check, reporter/CLI gates, parity and freshness now
pass. The branch's prior 24/24 CLI result on pre-scope product `358a2d3b` remains
iteration-only.

The earlier complete Stage1 check on the `19294e83` product found 1,225 missing
`Global.Read/Write` call grants across 87 compiler files, with no redundant
warnings; its 1,228-line log took 171.94 seconds and remains at
`build/global-grant-check-stage1-19294.log`. This is a historical migration
baseline from before the latest compiler source and scan optimization, not the
current inventory. The old 71-row Stage0 snapshot is narrower and is not
comparable. The isolated branch has now been seeded, provenance-checked and
qualified for strict source checking and actual CLI behavior. The next work is
to build and authenticate the matching proof pair, resolve backend declines on
the newest clean Studio tuple, and build the sealed app. The consumer check on the older product
passed Studio semantics but did not build or launch a current app. Keep
`-permissive` confined to explicit bypass controls.

### Next four outcomes

1. **Fix and qualify the function-value large-aggregate ABI.** The O0 sealed
   Studio app reproduced a crash when the 9,232-byte `Job` crossed a callback.
   Compiler commit `37091274` fixes the call convention and passes the focused
   1,024-byte callback regression with exact Stage1 provenance. The consumer
   owner rebuilt and sealed an O0 diagnostic app against compiler SHA
   `ad0e16c9…` and matching runtime `013d3174…`; opening the supplied FBX still
   makes the app quit. The fresh report faults at `arena_alloc + 0x58` with a
   null arena; `path_copy` expects the hidden region in x1. Compiler branch
   commit `d5a9b58a` adds region metadata and task-owned result arena transfer,
   but four core-file conflicts are being reconciled against newer compiler
   source. Its product and Studio retry are not qualified. `STUDIO_OPT_LEVEL`
   is committed as `0a39989b`, with O2 still default.
   O2 reached AArch64 DAGCombiner at about 28.4 GB physical memory and was
   stopped without an object. The grant-adoption branch carries the ABI fix as
   `12120f6b`.
2. **Finish compiler and proof integration.** Combined source `12120f6b` includes
   default Global grants, match-arm repair `9d2cf1b`, and the aggregate ABI fix.
   Rebuild and requalify the exact Stage1/runtime, then refresh the proof
   diagnostic inventory and prove and independently replay the original 265
   obligations.
3. **Finish current Studio acceptance.** Retry the FBX import with the qualified
   compiler, then build and seal the current app. The exact-identity O0 app still
   quits on the FBX; analyze its new crash report before playback checks. O2
   reached AArch64 DAGCombiner at about 28.4 GB physical memory and was stopped
   without an object, so optimized-build acceptance remains separate. Verify all
   337 frames, constant channels, repeated redraw, malformed-input refusal and
   read-only observer reveal.
4. **Close the prover memory and renderer release blockers.** Run all 73 engine
   reports with the unchanged 3 GiB / 120-second cap and preserve the original
   obligation inventory. In parallel after the optimized build lane clears,
   identify the renderer's measured retaining owner and prove the fix against the
   unchanged 8 MiB lifecycle gate. Then refresh Character Course packaging and
   promote the accepted tuple to hosted clean-checkout CI.

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
