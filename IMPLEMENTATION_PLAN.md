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

### Delivery milestones and ranking

Use dependencies and expected unblock value to choose the next task; table order
is not a reason to idle behind another owner. The current Studio app has two
separate open diagnostics: the latest matching-bundle crashes fault in
`Studio.clip.rehome` while drawing, with an invalid imported-GLB byte buffer
under investigation; an earlier import crash exposed a callback hidden-region
failure. Resolve and qualify these independently instead of treating the older
callback repair as an explanation for the latest reports. In parallel, close
the default-grant proof integration on one exact tuple. Then complete Studio's
full import, playback, redraw, malformed-input and observer acceptance on that
tuple. Close the measured prover-memory and renderer-lifecycle gates next; then
refresh the shipping package, exercise a second ordinary public-API project,
and promote the qualified tuple to hosted clean-checkout CI.

| Priority | Highest-return outcome | Why now |
|---|---|---|
| P0 | Fix and qualify global rehome for heap-owning values | Two Oct 9 crash reports match the current bundle UUID and fault at `Studio.clip.rehome+1160` while drawing. The consumer owner traced the invalid imported-GLB pointer to `publish_candidate` moving an affine `Clip` into a global without compiler-emitted global rehome; a caller-owned temporary arena can then be freed. Compiler commit `806772c7` builds Stage1 `5596ef20…`; its focused O0/O2/ASan regression passes with runtime `de964b7e…`. An earlier version passed 567 native backend checks, but the tightened final version's fast parity run has an unresolved 28/579 AST fixture mismatch. Resolve that against the parent baseline before Studio integration. Keep this distinct from the earlier `path_copy` callback-region report. See [current Studio evidence](IMPLEMENTATION_EVIDENCE.md). |
| P0 | Qualify callback-region ABI and result ownership independently | The earlier import failure showed `StudioFbxImportWorker.path_copy` receiving a missing hidden region through a generic callback. Compiler source `b719dbd5` forwards worker-result arenas and specializes arena requirements by return type; Stage1 `9acf976e…` and runtime `de964b7e…` pass provenance. Focused UAF/scalar-carrier, large-aggregate, hidden-region and result-lifetime regressions and a successful consumer retry remain open. O2 was stopped after more than 13 minutes in LLVM AArch64 DAGCombiner without an object; keep it a separate backend gate. See [historical callback evidence](IMPLEMENTATION_EVIDENCE.md). |
| P0 | Integrate default global grants into the proof and consumers | The latest installed Stage1 matches compiler source `b11e9121` (product `1505c598…`, runtime `013d3174…`). Direct compiler controls reject ungranted reads/writes and read-modify-write, allow exact grants, and confirm the explicit `-permissive` bypass. All five Character Course entrypoints, the uncached 216-test engine gate and the separate Maze C-ABI probe pass on this tuple. `scripts/run_tests.py` now runs those controls as an identity-pinned preflight, including indexed array reads/writes and global index expressions; the expanded preflight passed 32/32 controls on this exact product. The portable CI workflow also runs the grant-harness and hosted-toolchain policy suites on Windows, macOS and Linux. Environmental-effects and physics-interactables now pass strict checks; their visual/native self-tests pass as well. The compiler's 28-case inferred-row helper remains blocked by its report-format mismatch; the proof pair, full engine-wide strict-consumer census and freshly linked Course/Studio consumers remain open. See [current grant evidence](docs/validation/global-mutable-grants.md#current-b11e9121-engine-gate) and [public example evidence](docs/validation/global-grant-public-examples.md). |
| P0 | Build and exercise the exact Studio consumer tuple | The shared-grid converter bridge passes strict controls. The b719dbd5 O0 bundle is identifiable, but the matching-bundle crashes at `Studio.clip.rehome+1160` and the disabled FBX chooser prevent current playback acceptance. First resolve the backing-store failure and separately qualify the callback-region repair. Then verify all 337 frames, constant channels, repeated redraw, malformed-input refusal and observer reveal in one exact bundle. O2 remains separate and unresolved after its DAGCombiner stop. |
| P0 | Qualify the repaired prover under its original memory budget | The b719dbd5 prover rebuild did not qualify: it reported missing `Global.Read/Write` annotations in proof and compiler standard-library sources. Preserve the original 3 GiB / 120-second budget and all 265 obligations, five CLI regressions and 73 reports; repair only the reported grant gaps on a matching tuple, then rebuild and independently replay. |
| P0 | Diagnose the measured renderer lifecycle memory failure | The full native sequence intermittently exceeds its 8 MiB footprint allowance without matching heap/GPU growth. VM-region input hashes are refreshed at engine `fa451328`; the committed process-group watchdog enforces 3 GiB aggregate RSS / 180 seconds and has six passing controls. The exact native command and log/report paths are in the ignored preparation JSON. Run it only after the active Studio and UI native checks release the slot, then compare baseline and peak-cycle tag deltas without changing warmup, cycles, or the 8 MiB gate. See [the VM-region diagnosis note](docs/validation/effect-memory-vm-regions.md). |
| P1 | Restore the source-length gate — complete in `9e40976a` | Split asset-cook validation and macOS launcher generation into focused modules, separated launcher tests, and included the new configuration module in cook-cache identity. The full 600-line gate and 91 focused tests pass. See [validation](docs/validation/source-length-policy.md). |
| P1 | Ship a fresh Character Course package and automate the qualified tuple | Converts the repaired path into a usable deliverable, then prevents repeated manual qualification. |
| P1 | **Complete —** package and exercise the existing Maze public-API project | The headless gameplay, native SDL3/Metal smoke and nine relocated-package controls now pass on one exact compiler/runtime tuple; see [validation](docs/validation/maze-global-grants.md). Standalone release relocation with external shaders and dylibs remains a separate Q02 gate. |

### Ordered release work

| Order | Concrete deliverable and return | Acceptance / stop condition |
|---|---|---|
| 1 | **Fix and qualify global rehome for heap-owning values.** The consumer owner traced the current `Studio.clip.rehome+1160` fault to `publish_candidate` moving an affine `Clip` with nested GLB document buffers into a global; compiler output appeared to omit the global rehome, leaving pointers into the caller's temporary arena. Compiler commit `806772c7` builds Stage1 `5596ef20…` and passes the focused optional-aggregate handoff regression at O0, O2 and ASan. The tightened source's fast parity profile currently reports 28/579 `-emit ast` fixture differences; the cause and whether they predate this change are unresolved. | Resolve the AST parity result against the parent baseline, then pass the final fast and self-host/native checks before integrating the exact compiler/runtime tuple. Rebuild the same Studio app and verify the current draw/import path survives after the caller's region closes. Keep the separate callback-region ABI repair as its own gate; app acceptance remains required. |
| 2 | **Qualify callback-region ABI/result ownership separately — Q01/Q03.** An earlier import crash showed `path_copy` losing its hidden arena through a generic callback. The `b719dbd5` repair and Stage1/runtime provenance are recorded; the latest `Studio.clip.rehome` reports do not qualify that repair. | Pass focused UAF/scalar-carrier, large-aggregate, hidden-region and result-lifetime regressions, then retry the original import. Keep O2's AArch64 DAGCombiner stop as a separate backend gate. |
| 3 | **Integrate compiler default grants — Q01.** Installed Stage1 `1505c598…` and runtime `013d3174…` pass provenance and direct CLI controls, including `-permissive`. Five Course entrypoints, the uncached engine gate and Maze C-ABI probe pass. The runner's expanded preflight passes 32/32, covering indexed arrays and global index expressions; its refusal matcher rejects unrelated syntax errors, and 11/11 integration controls pass. Maze passes headless gameplay, native SDL3/Metal build/run and all nine relocated-package controls. Environmental-effects and physics-interactables now pass strict checks and their native smoke/self-test on the saved `b11e9121` Stage1/runtime pair; see [example grant evidence](docs/validation/global-grant-public-examples.md). | Fix the inferred-row helper's report mismatch without weakening checks. Rebuild and independently replay the proof product on a matching compiler/Core/runtime tuple; retain all 265 obligations, five CLI regressions and 73 engine reports. Then rebuild Course and Studio on the accepted tuple. |
| 4 | **Complete current-tuple Studio consumer acceptance — Q01/Q03.** Consumer commit `1e98f075` aligns multi-key FBX tracks; its bounded bridge regression passes on the fresh compiler/runtime tuple and preserves source bytes. Current playback remains blocked by the matching-bundle `Studio.clip.rehome` crash and disabled FBX chooser. | After resolving the current crash and qualifying the callback path, verify all 337 frames, constant channels, repeated redraw, malformed-input refusal and observer reveal in the same exact app bundle. |
| 5 | **Qualify the repaired prover memory path — Q01.** Current-source preflight passes on the last authenticated compiler/runtime pair. The Character Course project now passes strict semantic checks on the pulled Stage1, but it has not been freshly linked or run. Rerun the complete consumer check sequentially because overlapping runs raced in shared outputs. Apply the builtin-worklist and custom-pop repairs whose 25-obligation diagnostic harness fell from 757,284,864 to 16,089,088 bytes. That is diagnostic evidence only; original-input and full-inventory acceptance remain open. | The current clean pair passes the original first input under 3 GiB / 120 seconds with all 265 obligations proved and independently replayed, then the five CLI regressions and all 73 engine reports. Preserve the original 4,246 obligations, run immutable snapshot census/matrix afterward, and classify all nine historical timeouts. |
| 6 | **Finish actual redraw/reveal and observer integration.** Consume the qualified mesh repair and read-only observer in the real Studio path. This closes a public consumer contract and confirms the native guards through the app. | Studio redraws valid geometry unchanged; malformed inverse-bind, influence, joint and triangle shapes fail before output changes. Observer registration/reveal passes for original, quarantined, conflicting and uncertain locations without gaining restore/delete authority. |
| 7 | **Resolve renderer lifecycle footprint — R17/Q01.** The full native sequence has an intermittent footprint increase with no matching heap/GPU growth. The VM-region inventory is rehashed at engine `fa451328`; its process-group watchdog enforces aggregate RSS/time budgets and writes separate diagnostic evidence. | After explicit Studio/UI slot handoff, run the exact prepared command with one job, the original warmup/cycles and 8 MiB allowance. Inspect the real peak cycle before changing resource lifetimes; a retry alone is not a diagnosis. |
| 8 | **Restore the source-length gate — complete in `9e40976a`.** Split the three over-limit Python files by responsibility and fingerprint the extracted asset-cook configuration module. | `python3 scripts/check_source_length.py` passes; 41 packaging, 48 build/run and asset-cook, and 2 dense-cook tests pass. Full details and log paths are in [validation](docs/validation/source-length-policy.md). |
| 9 | **Refresh and relocate Character Course — Q02/Q04.** All five current public entrypoints pass strict project-context checking on the pulled compiler after local grant adoption. The previous optimized package predates this compiler/source tuple; rebuild and package once the current compiler pair is promoted. | Generated outputs match; resource hashes/notices are complete; relocated offline startup, restart and teardown pass with source/Homebrew denied. Keep signing, legal and separate-machine acceptance open. |
| 10 | **Rehearse the existing Maze project as the second ordinary application — Q07a/Q02 — acceptance complete.** The existing public-API client exposed and now scopes the default `Global.Read/Write` grants required by world construction, scene presentation and native runtime calls. On saved compiler source `b11e9121` and its matching runtime, the headless game route, native SDL3/Metal scripted app, all nine relocation/package controls and packaged-shader cold/warm/invalidation controls pass. Exact commands, compiler/runtime/artifact identities and limits are in [Maze grant acceptance](docs/validation/maze-global-grants.md). | Completed source slice: `examples/maze/main.elisa`, `examples/maze/native_client.elisa`, all three native entrypoints and `scripts/maze_game.elisascript`; real executable and cooked bundle hashes are retained in the validation note. The independent fresh Character Course package, hosted CI and compiler/proof gates remain open. |
| 11 | **Qualify hosted clean-checkout CI — Q03.** The `b11e9121` compiler pin matches the verified upstream `main`; the lock records all four observed heads, CI checks for drift, and hosted execution stays blocked until proof qualification. Core/proof/ElisaScript pins lag their heads and remain unchanged until the exact tuple qualifies. | Pass strict proof build/replay, the original 265 obligations, five CLI regressions and 73 engine reports on one tuple, then run hosted clean-checkout CI with retained provisioning/build/proof/package artifacts. GPU qualification stays separate. |
| 12 | **Finish gameplay and physical acceptance — Q07a/Q06.** Extend the course route for unsampled win/fall, input, audio and Jolt/GPU ownership outcomes. | The complete playable loop and reload/restart/teardown baselines pass. Physical checks require hardware evidence; unavailable hardware does not block ready local work. |

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
   boundaries exposed by those runs. The O2 attempt on compiler `9d2cf1b` stopped
   in AArch64 SelectionDAG/DAGCombiner without an object.
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
| Current Studio crash and proof freshness remain open | Resolve the matching-bundle `Studio.clip.rehome` crash, qualify the earlier callback-region path separately, refresh and authenticate the proof pair, then run playback/redraw/reveal acceptance. After explicit slot handoff, run `physics_rig` and the latest Studio character regression sequentially. | Mocap source commit `b79608a7` and UI commit `65f370f3` passed strict app semantics on the earlier snapshot. The clean `19294e83` compiler product passed detached provenance, but its tuple had six codegen declines and the proof frontend pin `2a3dce66` was stale. A newer O0 diagnostic bundle exists and matches the two crash reports at `Studio.clip.rehome+1160`; it is not accepted. The shared compiler checkout has uncommitted generic-codegen edits and must be reseeded before they can qualify. No physics executable is qualified. |
| Backend repair complete; current-source prover pair required | Qualify repaired prover memory on original engine input | Integrate default-grant compiler with backend `e791ec50` and latest clean prover repairs, then run unchanged first input under 3 GiB / 120 seconds. Follow with five CLI regressions and all 73 engine reports; diagnostic-harness success does not close these gates. |
| Consumer integration ready | Actual Studio mesh redraw and observer reveal | Use the current mesh repair and retained observer-header identity; validate the public API in the real consumer. |
| Ready without a heavy compile slot | Reproduce a concrete ordinary-project obstacle — Q07/Q02 | Publication rollback, manifest identity and package resource controls are implemented and registered in the unit stage. Walk the existing author workflow and implement only a newly demonstrated failure that prevents a runnable public-API package. End the slice when that failure is repaired; full native and hosted unit-stage execution remain open. |
| Ready after coordinated heavy slot | Renderer lifecycle diagnosis and repair | Observe the failing full sequence, identify the retained owner/domain, then repair its lifetime. Keep original cycles, warmup and 8 MiB allowance. |
| Ready after compatible tuple and native gate | Fresh package and ordinary author workflow | Reuse one clean optimized build for Character Course relocation and the existing Maze public-API application. Console image cooking and native package reading are preparation; the packaged application remains open. |

While Studio and prover acceptance run, select source work with an independent
acceptance path. Completed publication safeguards need actual package consumption;
additional fault controls alone do not outrank release acceptance.
Reserve each heavy slot through the existing coordination; record its
explicit handoff and terminal result in the validation record, since slot ownership
changes during execution. Defer census, broad native retries and package rebuilds
until their prerequisite can produce a meaningful acceptance result.

### Historical compiler and consumer status — 2026-10-08 baseline

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
The b719dbd5 callback/result-region repair passes source provenance, and a fresh
O0 diagnostic bundle was built against its Stage1/runtime pair. That app opens
to “Workspace available”; the consumer owner is still checking the FBX path
after desktop automation lost its window binding. This is not release
acceptance. The matching proof rebuild failed on missing default
`Global.Read/Write` annotations in proof/compiler sources, and the O2 build was
stopped after more than 13 minutes in LLVM AArch64 DAGCombiner without an
object. Keep strict grants and provenance; the proof pair, focused callback
regressions, playback/redraw and observer checks remain open. The renderer
VM-region preparation is now rehashed at engine `fa451328` across 1,186 inputs,
with a committed watchdog for its 3 GiB / 180-second cap. Run it only after
explicit Studio/UI slot handoff; its current tuple and command are recorded in
`build/validation/effect-memory-vm-regions-next-run-preparation.json`.

### Historical compiler/toolchain checkpoint — 2026-10-09, before the combined candidate

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

### Next four outcomes — current 2026-10-09 checkpoint

1. **Resolve the current rehome backing-store crash.** Two crash reports match
   the current O0 bundle UUID and fault at `Studio.clip.rehome+1160` while
   drawing. The consumer owner traces an invalid imported-GLB document byte
   buffer; the exact ownership transition and root cause remain unverified.
   The FBX chooser currently leaves Open disabled, so playback has not been
   repeated. Keep this gate separate from the earlier callback hidden-region
   report, whose `b719dbd5` compiler repair still needs focused regression and
   consumer qualification.
2. **Keep Global grants enforced and close proof integration.** Installed Stage1
   `1505c598…` passes the expanded engine preflight (32/32), including indexed
   array reads/writes and a global cursor; the runner's fail-closed integration
   controls pass 11/11. The prior exact-tuple engine suite passed 216/216, all
   five Course entrypoints passed strict checking, and Maze C-ABI embedding
   passed. The 28-case inferred-row reporter mismatch and proof rebuild remain
   open. Fix the reporter without weakening checks, repair only proof grant
   diagnostics on the exact tuple, then preserve and independently replay all
   265 obligations, five CLI regressions and 73 reports.
3. **Qualify the callback-region repair and complete Studio playback/observer
   acceptance.** Pass focused UAF, scalar-carrier, large-aggregate,
   hidden-region and result-lifetime controls, then use one exact app bundle to
   verify all 337 animation frames, constant channels, repeated redraw,
   malformed-input refusal and read-only observer reveal. The diagnostic bundle
   is not a release acceptance.
4. **Diagnose renderer lifecycle footprint, then refresh release artifacts.**
   The VM-region inputs are current at engine `fa451328`; the new process-group
   watchdog enforces the exact 3 GiB/180-second limit and has six passing
   controls. Wait for explicit Studio/UI slot release, run the original sequence
   with unchanged one-job/warmup/cycle/8 MiB settings, and inspect the actual
   peak-cycle tags. Then rebuild/relocate Character Course, rehearse the second
   public-API project and promote the qualified tuple to hosted CI.

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
