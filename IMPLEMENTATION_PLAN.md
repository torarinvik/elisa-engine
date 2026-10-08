# Elisa Engine — native implementation backlog

**Updated:** 2026-10-08. **Focus:** reliable, playable, packaged games on Wicked + SDL3.
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
4. Follow the current compiler [STYLE_GUIDE.md](../Elisa-compiler/STYLE_GUIDE.md) in new and touched code: last-expression returns, block initializers for temporary chains, loop-header captures for counters, and loop expressions with explicit `->` results (tuples for multiple accumulators, `break VALUE if COND` for searches). Compiler main `3030df2d` adds accumulator diagnostics and storage reuse for scalar/optional-scalar loop results; `5d6b779c` recognizes guarded updates such as running maxima. Installed `8006b660` passes 215 uncached runtime tests and builds the paired prover; the uncached proof sweep now passes 73/73 with prover `cb316eaa` and the fixed-index replay shortcut removed (the separate scalar-copy snapshot reproducer remains open). Full matrix/shared/native qualification remains open (see [installation evidence](docs/validation/compiler-8006b660-install.md)). Guide rechecked at compiler `cfd0203a` on 2026-10-08 (guide revision still `44b81b1a`) marks owned value-threading and builtin container value forms as working in Stage1. Compiler `cb10dd72` with Script `62928532` now passes the full native gate, all six reported stages and 30 application smokes ([evidence](docs/validation/launcher-error-guard-flow.md)); separate GLB memory growth is repaired in isolated compiler `04761c68`, which passes 215 uncached runtime tests within the original 3 GiB cap ([evidence](docs/validation/counted-fill-memory-growth.md)). Hardened follow-up `0baaa951` safely declines malformed helpers, passes all 215 uncached runtime tests and is integrated in compiler main; its paired prover build and 73-report engine sweep pass. Newer `cfd0203a` passes runtime/proof/shared checks. Primitive-shadow follow-up `52d60fcf` also passes 215 uncached runtime tests, all 73 engine reports with zero diagnostics, focused launcher controls and matching shared qualification; full native qualification is running, while full prover compatibility remains open. Use named `region` scopes for side-effect temporaries only when their allocations may end there; keep backing buffers alive for borrowed views. Use helpers when loop jumps prevent nesting. A region ends allocation lifetime too: values that must escape need a block initializer or helper with appropriate ownership. Use `def Type() -> Type` constructors and named constants; prefer suitable standard-library facilities.
5. Use qualified dependencies, small public surfaces, private owner fields, and phase-limited, disjoint call borrows. The current compiler checks overlapping borrows through reference locals, returned/conditional references, containers and function values; audit those paths when touching a call. Split borrows across disjoint fields or pass scalars by value; assign results where global mutation conflicts with a borrow. Unsafe code is opaque to this check and needs explicit alias review. On qualified Stage1 products, prefer owned value-threading (`x <- f(x)`) and builtin container value forms in touched code; return the same owning type (or a two-result tuple), omit `mutable` on the threaded parameter, and put owned fields back in the same statement. Keep reference APIs for borrowed owners, arenas and side-effect-only calls. Two-result builtin updates must target the original container first; audit every return, expression-position caller and function-value use before converting a helper, because those uses retain by-value semantics. Scalar input/result helpers are ordinary value functions; views are not threaded owners. These forms have compiler parity evidence, not an engine performance gain; see [targeted adoption rules](docs/plans/runtime-next-slices.md#compiler-style-guidance-applied-to-the-active-queue). Use `-Wnever-leak=strict` to triage touched code: gentle mode reports only rewritable accumulators; strict also explains candidates blocked by loop reads, later mutation, non-scalar storage or outer jumps. For touched accumulator sites, prefer supported scalar/optional-scalar loop results, tuple yields for multiple results and optional search results initialized to null; preserve zero-iteration values, evaluation order and borrow lifetimes. Treat strict findings without a valid rewrite as diagnostic guidance, not an instruction to force a conversion. Enable `-Werror=never-leak=strict` per directory once clean; avoid style-only sweeps. Keep vendor types out of public Elisa interfaces and files below 600 lines; split by responsibility.
   New guide guidance (`e3087e23`, read at compiler `b26659e2`): after qualifying the selected product, prefer comprehensions for touched pure collection builders and labelled loop results for nested searches. Ordinary block initializers may contain loop exits; exits from capture blocks remain forbidden. Triage `push loop` and `value-thread` lint findings; preserve ordering, allocation lifetime and proof replay. Prioritize builders encountered during package refresh and ordinary-project rehearsal; see the targeted adoption rules above.
6. Preserve real error unions and failure atomicity. Define resource/thread/allocator ownership, callback lifetime, cancellation, capacity behavior, and destruction order.
7. A compiler, ElisaScript, or prover limitation blocking this style becomes a minimized regression and a fix in its owning repository. Do not flatten scopes or weaken checks as the permanent workaround; record the required toolchain revision. Prover holes, meaning engine constructs or properties the prover cannot yet check, are implemented in the `../elisa-engine-proof` worktree (branch `elisa-engine-proof` of `elisa-proof`), each with an accepted and a rejected regression example; `check` uses its `build/elisa-proof` by default. Before each prover change, merge the committed gains of the other `elisa-proof` branches (`main`, `codex/wasmbrowser-proof`) so engine proofs run on the current prover.
8. Completion requires a public API used by real gameplay/editor code, positive and adversarial tests, native evidence where applicable, and documentation. Mocks validate contracts but cannot establish library or GPU integration. Tests must assert outcomes. Write proofs alongside implementations: each slice adds or extends an implementation-linked proof in `proof/` for its pure policy (bounds, wrap/clamp arithmetic, identity and ordering, capacity, state transitions). The proof includes the real source, and shared pure transitions carry their own `requires`/`ensure`. `check` runs every `proof/*.elisa` and records each report. A property the prover cannot yet express becomes a prover task, not an omitted proof.
9. Record performance claims in optimized builds: warm-up policy, hardware, scene size, CPU/GPU time, p50/p95/p99, allocations, and peak memory. Qualifiers and block syntax should add no runtime machinery; verify generated code when that is the claim.
10. Change `[ ]` to `[x]` only with commit, command, result, artifact path, and limitations recorded in a linked `docs/validation/` note. Update capability labels precisely. Commit coherent changes. Do not mark a subsystem complete from its first smoke test. The note also names the slice's proof file, its obligation count, and what remains unproved.
11. If blocked, record the exact cause and a concrete prerequisite task, then continue another ready task. Hardware-unavailable checks remain unverified, never silently green. Product decisions such as the project's license do not block unrelated implementation.

## Active delivery queue — highest return first

Work on one bounded deliverable at a time. Rank by observed failures, shipping dependencies, reuse across clients and decisive acceptance.
The full subsystem backlog remains the completion scope; each slice retains implementation-linked proofs and adversarial checks (contract items 7–8).

**Current baseline:** frozen compiler `52d60fcf` and its matching runtime pass
215 uncached runtime tests and the shared gate. Integrated prover `f593c886`
passes all 73 engine reports. Isolated replay repair `14ae4a02` also passes
73 uncached reports / 4,246 obligations, the adversarial source-binding harness,
and scalar-copy/next-write controls; its immutable pair is
`f43713c9ff9e4f559d46c4b1ed68331b`. It is not yet promoted into the production
prover worktree. The older `f593c886` full matrix is now terminal with 54
failed steps. New isolated producer repair `73e2c25a`, paired generation
`dd326a16639a4f86bd650042a9e0a2dc`, also passes 73 uncached reports / 4,246
obligations and restores the narrowed-write case to 4/4 replayed. Its seven
compiled entry-count source controls pass. The broader symbolic suite remains
failed: isolated indexed-copy repair `eb69c836` now passes its focused bounds
and forged-source controls, plus 73 uncached engine reports / 4,246 obligations.
Prioritize the remaining branch-join, symbolic and partition failures.
Branch premise repair `c3c153c5` retains the same engine inventory; follow-up
`8cf80b79` restricts premise search to scalar comparisons and passes the
compiled immutable-widening/reflexivity regression. The joined mutable local
was isolated by a two-premise audit that derived its bound but could not replay
it (exit 11). Repair `58371cb4` now reconstructs both source branch paths,
passes 12 forged trace/source controls and restores the full branch corpus
with zero gaps. Clean paired generation `e00d4246a9f64bdd8ec16a521f5ce890`
retains all 73 engine reports / 4,246 obligations with independent replay and
no trusted assumptions. The semantic corpus now passes 13/15 workloads;
loop-entry repair `de4017e8`, clean generation
`2a1ef76a708c421f97777e0aa76b4ac6`, retains all 73 reports / 4,246 obligations
and removes both symbolic workloads' replay gaps. Its original binding,
preservation refusal and 11 forged-source controls pass. The semantic corpus
now passes 14/15: accepted symbolic retains two uncertified bubble preservation
and return goals (79/81 proven, 79/79 replayed). A standalone branch iteration
also leaves its quantified postcondition uncertified, with zero replay gaps.
Prioritize quantified branch consequence generation before loop exit. The
standalone swap and keep arms prove/replay 8/8 and 4/4 respectively. The join
currently refuses a candidate when neither arm's substituted step equals the
joined expression; the new maximum therefore needs a certificate authenticating
both substitutions and source paths. Preserve that admission check until the
replacement certificate independently validates both arms, including collection
writes and the condition's pre-state. Candidate enumeration alone cannot close
this gap. Preserve all
existing source, mutation and forged-symbol refusals; a passing engine sweep
does not establish full prover compatibility.
The earlier pinned native run failed at Character Course relaunch; its focused
retry passes. The newer explicit-audio repeat passes relaunch but fails async
capture GPU timing and the documentation length policy. Linked-document
splitting restores the length check; the focused capture retry passes with
failure-only timestamp diagnostics, without reproducing the original cause. Full matrix/shared/native compatibility on the
repaired pair remains open. See [replay repair evidence](docs/validation/action-input-replay.md)
and [compiler/native evidence](docs/validation/counted-fill-memory-growth.md).
AudioAnimEvents 56/56 and SoundAssets 157/157 retain zero replay gaps. The
historical relocated bundle still needs corrected guide outputs and a fresh
package. See [targeted task details](docs/plans/runtime-next-slices.md).

| Order | Highest-return deliverable | Acceptance / stop condition |
|---|---|---|
| 1 | **Implementation-linked engine sweep restored — Q01.** Prover `cb316eaa` completes ActionInput context 265/265 and all 73 uncached reports; retain the [enum payload/source controls](docs/validation/reference-free-enum-summary.md). The separate guarded scalar-copy snapshot now proves and replays 5/5 with preserved mutation refusals, integrated in prover `f593c886`; full toolchain compatibility remains open. SoundAssets now replays 157/157; preserve its local-digit, source-binding and mutation controls. AudioAnimEvents now replays 56/56; preserve its bounded dispatch and enum resource controls. AudioVirtual now replays 109/109; preserve its source-call, indexed-capture and loop-entry controls. Keep each minimized accepted/rejected pair in the owning prover repository. | All 73 proofs pass uncached with complete original obligation inventories and replayed certificates. Never remove failing rows, weaken contracts, or count a passing helper as repaired engine coverage. |
| 2 | **Qualify one reproducible toolchain — Q01/Q03.** Use the installed compiler and matching runtime/parser sources; triage the full prover regression matrix and then rerun shared and native gates. Repair failures exposed by those runs before adding unrelated features. | Full prover matrix, shared check and native gate pass on recorded product hashes and source revisions, with structured failure/skip artifacts. Hardware-unavailable stages remain unverified. |
| 3 | **Refresh the shipping client — Q02/Q04.** Rebuild optimized Character Course from clean source with the corrected generated guide assets; package and run the existing relocated/offline lifecycle checks. | Generated outputs match, all resource hashes and notices are checked, offline startup/restart/teardown pass with source and Homebrew denied. Keep separate-machine, signing and legal acceptance open until established. |
| 4 | **Eliminate workstation-only setup — Q03.** Update hosted full-SHA pins only after compatible compiler/core/prover/ElisaScript products are qualified; exercise fail-closed bootstrap from a fresh checkout. The former unpublished-compiler blocker is superseded. | An actual hosted headless run provisions, builds and retains tests/proof/package artifacts. GPU evidence is separate. Do not call a workflow definition or local preflight a passing CI run. |
| 5 | **Make the ordinary project path repeatable — Q07a/Q02.** Rehearse the existing new-project-to-package instructions outside this checkout, using public APIs and current cooked content. Fix the first demonstrated authoring, asset-discovery or failure-diagnostic gap. | A fresh ordinary Elisa project builds and packages without sample-specific native exports or undocumented steps; invalid/missing resources produce actionable errors. Record the command sequence and exact products. |
| 6 | **Close real gameplay acceptance and ownership gaps — Q07a/Q06.** Finish visible course traversal/win/fall and physical input/audio checks when available. Extend the existing lifecycle stress only for currently unsampled Jolt/GPU resource ownership or a reproduced leak. | Outcome checks cover the complete playable loop; repeated reload/restart/teardown returns measured live resources to baseline. Physical checks need actual hardware evidence and must not block independent ready work. |

**Selection rule:** fix the first actionable failure in this queue. Within proof work,
prefer a shared fix that closes multiple real engine reports over additional standalone
proof features. Broader source mapping is justified by a concrete failing report.
Do not rerun unchanged expensive benchmarks or start another renderer/animation/audio
showcase while release gates remain red. Once a slice passes, advance to the next;
record new failures and promotion triggers in the targeted plan. The mocap M track
is active for the FBX surface gap in [its coordinated plan](docs/plans/mocap-engine-track.md).
External hardware, signing or license decisions do not block ready local work.

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
