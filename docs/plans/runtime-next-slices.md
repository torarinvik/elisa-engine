# Targeted runtime delivery — 2026-10-09

This refines the active queue in `IMPLEMENTATION_PLAN.md`. The full backlog
remains open. Select a concrete defect or missing acceptance result in the
Character Course before promoting a subsystem-wide feature.

## Current delivery order

The [active delivery queue](../../IMPLEMENTATION_PLAN.md#active-delivery-queue--highest-return-first)
is the only scheduling authority and was refreshed on 2026-10-09. Its current
highest-return gates start with the global rehome defect for owning values,
followed by the separate callback-region ABI repair and default-grant proof
integration. Exact Studio acceptance, the original-budget prover run, native
redraw/reveal, and renderer lifecycle diagnosis follow. Package refresh,
ordinary-project rehearsal, hosted CI, and physical gameplay acceptance come
after those prerequisites. The technical slices below are supporting guidance,
not a second queue.

For the second ordinary-application gate, use the existing
`examples/maze` consumer: its manifest cooks an authored PNG texture bundle, its
client uses the public render and asynchronous asset APIs, and
`scripts/packaged_maze_smoke.py` already checks relocated execution with the
checkout denied plus missing, escaping, corrupted and undeclared-dependency
failures. Its headless route, native SDL3/Metal scripted app and nine packaged
controls now pass on the saved `b11e9121` Stage1/runtime pair; the consumer's
default `Global.Read/Write` adoption and exact artifact identities are recorded
in [`docs/validation/maze-global-grants.md`](../validation/maze-global-grants.md).
Keep fresh Character Course relocation and hosted CI as separate acceptance.

The mocap consumer still needs provider registration and actual Studio reveal
acceptance. The read-only trash location observer and focused native identity,
readonly-operation, bounded-output and uncertainty controls are qualified at
engine-mocap `3f60d5ed`; its clean source and header tuple has been handed off.
Require full retained-binding matching and fresh observation before reveal.
Keep restore/deletion authority outside this observer.

Historical repair details remain in linked validation notes. They do not define a
second queue or establish current compatibility. Keep the full subsystem backlog,
separate-machine packaging, signing and legal acceptance open until qualified.

## Completed local automated slices — retained evidence

| Order | Deliverable | Acceptance and value |
| --- | --- | --- |
| 1 (automated complete) | Focus-loss recovery through the live course loop | Hold movement and crouch, lose focus, verify automatic pause and cleared actions, then resume and finish the existing route. Run through SDL event translation; record an OS window-switch separately. Prevents stuck movement and unintended gameplay after switching applications. |
| 2 (automated complete) | Resize the running course in play, pause and controls | Verify logical/drawable dimensions, camera aspect and readable HUD at small and large sizes. Reuse the existing native resize hook and retain captures. Fix clipping or stale layout exposed by the check. Physical DPI/display changes remain separate evidence. |
| 3 (automated complete) | One production worker-event consumer under `WorldSchedule` | Reuse Jolt's existing bounded contact queue. Resolve contact participants through checked body bindings before routing a crate impact to gameplay/audio. Reject stale world epochs and wrong access phases; retain overflow and unsubscribe evidence. Avoid constructing another public scheduler or generic queue before a producer needs it. |
| 4 (automated complete) | Measure the course's actual audio workload | Record voice/stream counts, memory and underruns during route, pause, reload and teardown. Exercise existing cancellation/device recovery rather than adding another audio feature. Physical listening and unplug/reconnect remain explicitly unverified until performed. |
| 5 (local automated complete) | Cook and measure the pinned authored Cesium Man rig | Reuse Ozz's local Cesium Man glTF fixture (19 skin joints, one clip; retain its CC BY attribution). Record the explicit neutral-material/weight-normalized benchmark variant because the pinned source has unsupported sampler state and invalid weight sums; keep importer rejection intact. Exercise the normal geometry/keyed-contract cook and eight independent rendered instances. Target p99 <=1 ms per eight-instance CPU update and zero steady allocations; record package/source hashes and sampled memory. Fix an actual import or sampling defect before adding animation graph features. |
| 6 (local automated complete) | Rebuild and relocate the updated game | Verify optimized clean provenance, resource hashes, offline startup and graceful teardown after the preceding runtime changes. Keep separate-machine testing, signing and legal review visible as external acceptance work. |

The existing live-input pilot already covers pause/resume, held crouch,
traversal, win, restart and fall. Extend that path to establish focus recovery;
do not add another end-to-end harness for the same route. Existing native
resize tests and course HUD helpers are the starting point for item 2.

## Decision rules

- Complete one slice through implementation, implementation-linked policy
  proof, outcome assertions and evidence before starting another.
- Preserve existing APIs and ownership. Native contacts already arrive through
  copied, bounded records; native entity keys must never be treated as World
  entity IDs. World identity resolution belongs in Elisa's binding owner.
- Treat synthetic SDL events, visible OS interaction and physical hardware
  checks as distinct evidence. A synthetic gamepad mapping does not establish
  physical controller behavior.
- Run the focused gate after a change. Broaden testing when a shared contract
  changes or a focused failure indicates wider impact. Do not rerun unchanged
  expensive benchmarks merely to fill an evidence table.
- Performance work starts with a workload and target. The current keyed
  benchmark uses eight two-joint instances and measures CPU update calls;
  it does not establish production-rig cost or GPU frame time.
- Retain deferred editor, multiplayer, scale and platform tasks. Promote them
  when a named consumer or measured budget makes their acceptance actionable.

## Compiler style guidance applied to the active queue

Rechecked `../Elisa-compiler/STYLE_GUIDE.md` at compiler checkout `b11e9121`
on 2026-10-09; the latest guide change is `dd8aea22`. Section 7 confirms
borrow-exclusivity checks across reference locals, returned/conditional refs,
containers and function values. Its mutable-global subsection states that
reads require `Global.Read`, writes require `Global.Write`, and
read-modify-write requires both by default; function effects and local `can`
blocks grant access, callers carry the capability, and `-permissive` is for
diagnostic or migration workflows. Section 6 marks owned value-threading and
builtin container value forms as working in Stage1. Earlier guide changes also
cover comprehensions and labelled loop results.
The immutable 2ad7a165 product separately passes 215 uncached runtime tests
and 73/73 engine proofs; full prover matrix/shared/native qualification
remains open ([evidence](../validation/compiler-2ad7a165-qualification.md)).
The loop, region and strict-lint guidance is already in the main plan. Apply
these additional checks within implementation slices:

- Preserve the existing launcher acceptance when adopting the newer guide:
  recovered errors must leave the escaping error row, while fallback errors
  remain checked. Retain explicit Console.Write effects for diagnostic calls.
  Preserve guard boundaries and allocation lifetimes when scoping this work.
  The guard-aware launcher now executes quick and headless gates. Compiler
  `cb10dd72` repairs the narrow-pop numeric conversion exposed by cross-block
  verification; Script `62928532` passes the seven focused guard controls.
  This pair passes pinned headless and full native replay: the native run
  completes in 2,027.77s with all six reported stages and 30 application smokes
  passing. Later compiler replacement products still need their own native
  qualification ([evidence](../validation/launcher-error-guard-flow.md)).

- At mutable-reference call sites, check aliases through reference locals,
  returned and conditional references, containers and function values. Use
  disjoint fields or scalar inputs with returned results. Review Unsafe aliases
  explicitly because compiler borrow exclusivity does not inspect them.
- For the input proof repair, preserve scalar snapshot bounds at copy time.
  An immutable copy must keep its own bound after record mutation; equality to
  the record's current field must expire. Immediate pure indexed writes now
  replay authenticated field copies; the guarded immutable copy now retains its
  bound across mutation through independent copy-time source reconstruction.
  Unknown control-flow and type contexts remain refused.
  The two scoped `device_index` result bindings in `apply` now authenticate
  their source calls inside value-block initializers. Keep value
  blocks and loop results; preserve shadowing and mutation refusals in replay.
- Scope side-effect temporaries only after checking allocation escape and live
  views. Use block initializers or helpers when values need to escape; use a
  helper when a loop jump would leave a block with a `|capture|` header.
  Stage1 permits jumps out of ordinary block initializers; preserve that path
  without publishing an uninitialized result.
- Prioritize the native launcher's argument ownership: finish growing
  `owned_arguments` before collecting its element pointers into `argv`, then
  retain both owners through the process call. Inspect all five process paths
  in ElisaScript's `src/ir/interpret.elisa`; the pattern also occurs outside the
  two timeout paths. This applies the guide's backing-buffer lifetime rule
  to the current native prerequisite. A region must not free argument storage
  before the foreign call finishes; changing to value-threading alone does not
  make previously published pointers survive container growth.
  Implemented the two-pass collection in all five paths on 2026-10-08:
  the bounded Stage1 build removes all 25 argument-storage invalidation
  diagnostics. Other launcher compatibility diagnostics still prevent a
  runnable native gate at that point. The subsequent guard-aware launcher
  passes headless, build-gate and full native replay.
- Adopt strict lint as a diagnostic on touched code, then enable errors only
  for a clean directory. Preserve loop zero-iteration results and refuse forced
  rewrites where strict diagnostics explain why no valid rewrite exists.
- Prefer tuple yields when a touched loop produces several results and optional
  search results initialized to null when absence is part of the API. Use
  explicit `->` yields; bare loops in value position are invalid. Container
  value-threading is now available in Stage1; adopt it at owned update sites
  when already touching them and after qualifying the selected compiler product.
- When changing an accumulator to a loop result, keep the initializer as the
  zero-iteration result and preserve each early-break value. For optional search
  results, keep absence explicit as null; migrate an existing sentinel API only
  when its callers and contracts are updated together.
- A region must expire its locals and allocations while retaining valid facts
  about untouched outer scalars. The current input diagnostic isolates this
  boundary; qualify the prover repair against shadowing, rebinding, local escape
  and effectful calls before accepting it. Keep the production region in place.

### New guide features: targeted adoption after product qualification

- Prefer comprehensions for touched array builders that only push one result per
  selected element and read the collection after completion. Start with asset
  inventory and package metadata builders encountered in queue items 5–7.
  Preserve iteration/filter order, effects, output ordering and allocation lifetime.
  Unfiltered range/darray comprehensions can reserve the result once; measure
  actual memory or time before claiming an engine improvement. Retain the real
  GLB chunk-loading growth regression when changing allocation strategy.
- Nested comprehension clauses are Stage1 only and nest left to right; later
  binders may read earlier ones. Tuple elements destructure by position, but
  explicit tuple types need named fields. Dict/set forms need the matching
  Stage1 runtime. Bracket-free generators are unsupported.
- Use labelled loops and labelled break values for touched nested searches when
  they remove an escape flag while preserving the existing search contract.
  Qualify proof source mapping and replay for the exact new syntax before
  accepting a rewrite in implementation-linked engine proofs.
- Triage the new `push loop` and `value-thread` lint findings alongside existing
  accumulator findings. Gentle mode offers rewrites; strict mode also explains
  borrowed-owner, arena, global and effect-only candidates. Apply supported
  rewrites in files already needed by the active queue. Strict error enforcement
  requires reviewing unresolved candidates, even when gentle mode is clean.
- These additions describe current source support. The frozen `52d60fcf`
  qualification does not establish support for later guide changes; record and
  qualify the compiler/runtime/prover pair before adopting those features.

### Allocation lifetime: current consumer priority

- Apply guide sections 3 and 9 to the demonstrated mocap skin-cache lifetime
  failure and the separate current-toolchain GLB loading memory failure. Keep
  returned mesh arrays and cached skin output alive across producer return,
  redraw and allocation churn. Do not introduce a region around escaping data
  merely to satisfy the scope lint; audit generated arena arguments and releases.
- Current frozen `cb10dd72` runtime qualification is incomplete: both parallel
  and serial sweeps reached their RSS limits. The isolated `viewport_mesh` test
  reproduces growth while `GlbSkinMesh.load` reads the 53 MiB boxer GLB, before
  parsing. Temporary instrumented copies reproduce it at O0 too: capacity
  increases by exactly 64 KiB per read. Disabling only automatic reserve makes
  the unchanged real-asset test pass in 0.72s at 301,792 KiB peak RSS. The counted
  loop pre-reserve calls exact-size reserve on each chunk; repair its growth
  policy to retain amortized geometric growth before rerunning qualification.
  Isolated compiler commit `04761c68` now passes O0/O2 growth and explicit-reserve
  controls; the unchanged real-boxer test passes at 155,808 KiB peak RSS at O2.
  Its frozen compiler/runtime pair passes all 215 uncached runtime tests in
  25.54s at 1,303,936 KiB peak RSS under the original 3 GiB cap. Hardened
  follow-up `0baaa951` safely declines malformed helper metadata, passes the
  same 215 uncached runtime tests in 21.79s, and is integrated in compiler main.
  Its official paired prover build and 73-report engine sweep pass. The newer
  `cfd0203a` diagnostic repair and primitive-shadow follow-up `52d60fcf` pass
  215 runtime tests, 73 engine reports with zero diagnostics and shared gates.
  The matching `52d60fcf` launcher passes focused controls; its full native gate
  exposed the intermittent lifecycle footprint failure. Installation
  and full prover/shared/native compatibility remain open
  ([evidence](../validation/counted-fill-memory-growth.md)).
  Preserve the real asset acceptance and existing watchdog limits. This memory
  failure does not establish the cause of the client's older sealed crash.
- Use the guide's owned value forms only after lifetime correctness is established.
  Their documented equal machine code is no evidence of reduced allocations or
  repaired arena routing. Reuse the existing client and engine acceptance paths.

### Concrete asset-code candidates from the guide review

Rechecked the guide at `b11e9121` on 2026-10-09; the latest guide revision is
`dd8aea22`. Use these candidates when the owning consumer slice next
touches the file, after qualification of the selected product:

- `src/assets/gltf_cubic_sample.elisa`: the final four-element `normalized`
  builder is a simple comprehension candidate. Preserve component order,
  normalization arithmetic and returned allocation lifetime. The preceding
  `out` loop also accumulates `length_squared`; review that dependency before
  changing it. Measure sampling allocation/time with representative animation
  data before promoting this as a performance task.
- `src/assets/gltf_skeleton.elisa`: the initial `parent` and nested `track`
  fills are candidates for range comprehensions, including left-to-right
  nested clauses for the four track slots per node. Both arrays are mutated
  later, so retain mutable bindings. Verify slot count/order, node limits and
  subsequent indexed writes; building through a comprehension does not make
  the completed skeleton tables immutable.
- `src/assets/glb_document.elisa`: `find_node` and `find_animation` can use
  loop-result captures while preserving the public `-1` sentinel and first
  match. Introducing an early break also changes how many comparisons execute;
  review that separately. `node_name` and `animation_name` fill caller-borrowed
  output buffers, so retain those reference APIs and their capacity reuse.
- Keep GLB chunk loading's existing growth regression and memory budget as
  acceptance for any allocation rewrite. A comprehension's reserve-once claim
  applies to unfiltered range/darray builders; do not extend it to filtered
  or nested builders without measurement of the selected compiler product.

### Owned updates: targeted adoption

- Prefer `xs <- xs.push(v)`, `xs <- xs.clear()`, `xs, last <- xs.pop()`,
  `xs, item <- xs.remove_at(i)`, `d <- d.put(k, v)` and `d, found <- d.remove(k)`
  at existing owned update sites. The first target of a two-result builtin
  must be the original container. `remove_at` and dicts require the Stage1 runtime;
  darray `insert` has no supported in-place or value form yet.
- A threaded helper takes an owning container or struct by value without
  `mutable`, writes it and returns that same value on every return, optionally
  with one additional tuple result. Scalars and views are not threaded owners.
  Put a moved field back in the same statement (`s.items <- f(s.items)`).
- Retain mutable-reference helpers for fields reached through borrowed owners,
  arenas and side-effect-only operations. A move into a different result ends
  the original binding's usable lifetime. Do not drop the returned owner.
  Prefer scalar input/result assignment for a global that conflicts with a
  call borrow; scalar helpers are ordinary value functions, not owned threading.
- Inspect expression-position calls and function-value uses before converting a
  helper: those keep by-value semantics and prevent its other calls from using
  the in-place rewrite. Global threading uses a local copy; preserve reads of
  the global made by the callee.
- Highest return: qualify the new compiler product first, then use these forms
  in the next owned collection change needed by a named consumer. Preserve
  loop capture/result initialization, failure behavior and resource proofs.
  Avoid a separate API migration or style sweep. The guide records identical
  machine code at O0/O2 for its parity fixtures; this establishes no new engine
  speedup. Stage0 rejects writing these by-value parameters.
- For each consumer-driven adoption, record the exact owned call site and audit
  every return and caller before changing its signature. Keep expression-position
  and function-value callers in that audit. Require unchanged outputs, error
  paths, zero-iteration behavior and resource lifetimes in the slice's existing
  acceptance checks; retain moved-owner, borrowed-field and wrong tuple-target
  refusal coverage in the owning compiler. Promote the pattern only after the
  selected engine/prover path accepts it. A parser accepting the syntax alone
  is insufficient evidence for a public API migration.

### Next touched-code review

Apply these checks during the next defect fix or consumer-requested change:

1. Establish whether the update owns its receiver. An owned local or owned
   field can use the value form; a field reached through `State&` retains the
   reference form. Keep views and their backing buffers alive together.
2. Audit every return and caller before changing a helper signature. Every
   return must hand the owner back; expression-position and function-value
   calls retain by-value semantics. For tuple builtin updates, the first target
   must be the original container. Use ordinary scalar input/result functions
   for scalar updates.
3. Give temporary chains a block initializer and loop-produced values an
   explicit yield. Preserve initial values on empty loops and every early-break
   result. Use a helper when a loop jump prevents safe nesting.
4. Use a named region only when all allocations made inside may end there.
   Inspect escaped arrays, views, cached mesh data and foreign pointers before
   introducing it. Container syntax alone does not repair an allocation lifetime.
5. Read strict lint findings on the touched code and follow valid rewrites;
   enable strict errors only for a clean directory. Qualify the slice through
   its existing acceptance path on the selected Stage1 product. Stage0 cannot
   compile writable owned-in/owned-out parameters.

The latest guide revision checked on 2026-10-08 is `e3087e23`, including
nested comprehensions and reserve-once builders. Keep compiler qualification
and measured engine performance evidence attached to their exact products.
Bundle this guidance with the source change that adopts it; documentation
updates alone do not warrant a commit.

## Current evidence

Focus recovery passed the full native course smoke on 2026-10-07 (167.433s),
plus the focused state test and 41/41 implementation-linked proof obligations.
Synthetic SDL focus loss/restoration is established; an OS window switch
remains external acceptance. Resize/HUD automated acceptance passed: paging has its exhaustive
test and 20/20 proof obligations; real pause/play resize, small-menu captures
at three text sizes, paged SDL pointer clicks and full traversal passed
(230.790s). Physical pointer/DPI and tiny-window decisions remain open.
The checked Jolt contact consumer now passes the native pose gate (60.727s),
including wrong epochs, despawn, replacement, session restart, wrong phase,
subscription/access rejection and bounded overflow. The main live-input course
also passes real crate contact delivery through the scheduled Audio phase
(203.575s). The cell pilot passed (196.867s) after fixing a grounded jump
consumed by residual fall speed; its original rise threshold remains intact.
Classification has 8/8 implementation-linked proof obligations.
[Contact delivery evidence](../validation/world-physics-contacts.md) records the
remaining physical-listening limit. Actual course audio workload measurement now
passes the live route (194.134s) and binding reload (186.636s), with
zero stream underruns, bounded measured PCM/ring capacities and explicit
resource release. A measured screenshot-encoding stall was fixed without
increasing audio buffers; focused lifecycle/stream sanitizers pass.
[Audio workload evidence](../validation/course-audio-workload.md) keeps
physical listening/device changes and whole-audio-heap accounting separate.
The authored rig now cooks as an explicit benchmark variant, preserves global
joint ancestors and passes upright rendered deformation. Eight updates have
worst p99 139.375 microseconds and zero steady allocations using compiler
`96761822`. The 19 skin joints occupy 20 runtime nodes; see
[authored benchmark evidence](../validation/animation-authored-benchmark.md).
The updated clean-source game now passes relocated offline startup, exact
resource hashes and graceful teardown with Elisa/native `-O2` after fixing
hosted compiler flag forwarding. See
[updated package evidence](../validation/character-course-runtime-slices-package.md).
Separate-machine, signing, legal review and physical checks remain open.
See [resize evidence](../validation/course-resize.md).

`66911c90` routes effect payload consumption through `WorldSchedule`, with live
entity validation under its read token. The native effect smoke and the
environmental-effects capture passed. This establishes a scheduled consumer;
Jolt worker ingress and course crate audio consumption now use this schedule.
General runtime consumers and the wider W08 acceptance remain open.

See [course controls](../validation/course-controls.md),
[world events](../validation/world-events.md),
[physics queries](../validation/physics-queries.md) and
[Ozz animation](../validation/animation-ozz-service.md) for current results and
their limits.
