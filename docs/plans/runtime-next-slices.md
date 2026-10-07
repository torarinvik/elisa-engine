# Targeted runtime delivery — 2026-10-07

This refines the active queue in `IMPLEMENTATION_PLAN.md`. The full backlog
remains open. Select a concrete defect or missing acceptance result in the
Character Course before promoting a subsystem-wide feature.

## Next executable slices — revised priority

| Order | Concrete next action | Why now / acceptance |
| --- | --- | --- |
| 1 | Retain the actual capacity and slot guards in `ActionInput::bind_checked`, then preserve immutable scalar snapshot bounds. | Fixed-array count projection and immediate field-copy replay are authenticated; scoped device_index summaries replay. Read-only and conditional captured-search reproducers now pass 8/8 with zero gaps: bounded value-block effect/write analysis and nested initializer authentication are implemented. The actual binding-count guard is still absent before its copy, and three later slot bounds remain open. Source variants now isolate the lost binding-count guard to the verified read-only state_slot call: removing the conditional search leaves four findings, while replacing the call leaves only the three later slot findings. Establish source-authenticated read-only call handling for bounded searches with mutation/global/callback/alias refusal controls; never reuse initial equality after iteration. Then preserve independent copy bounds across mutation while expiring equality to current fields. Context remains 232/264 with 32 findings and zero gaps. See [search evidence](../validation/invariant-for-retention.md) and [input evidence](../validation/action-input-replay.md). |
| 2 | Close remaining indexed/borrowed `ActionInput::apply` bounds and finish both ActionInput reports. | Context report now proves/replays 232/264 with 32 findings and zero replay gaps; ordinary-loop retention repairs all rebind_checked findings. AudioAnimEvents is restored to 56/56; SoundAssets 157/157 and AudioVirtual 109/109 remain preservation controls. The uncached engine sweep is 71/73, with action_input_context and action_input_deadzone failing. Each fix needs an implementation-linked result and invalid controls; stop only at all 73 complete reports. |
| 3 | Run the full prover compatibility matrix, shared check and full native gate on one qualified immutable compiler product. | The full matrix finished with 73 failed steps. Clean paired generation 5333c304 now passes package JSON boundary/provenance checks; loop-state and captured-loop replay failures persist, alongside collection/loop-frame findings from the full run (see current input evidence). Repair those failures and rerun full qualification with exact product identities. Compiler 8006 runtime and non-proof shared stages pass. Native qualification first needs a rebuilt ElisaScript supporting explicit process timeouts and explicit selection of the existing pinned ../elisa-boxing-wickedengine checkout/archive ([evidence](../validation/compiler-8006b660-install.md)); sanitizer stages pass, application smokes remain skipped. Full prover compatibility is open. Capture terminal results, product/runtime hashes, all original counts and explicit skips. Fix actual failures without weakening acceptance. |
| 4 | Rebuild and relocate optimized Character Course with current generated guide assets. | The earlier bundle has historical pre-refresh guide files. Verify generated-output equality, exact resource hashes, offline startup, restart and graceful teardown; retain old evidence as historical. |
| 5 | Qualify compatible hosted pins and run clean-checkout headless CI. | Published compiler main now includes the formerly unavailable prerequisite. Verify compatible products before changing pins; retain actual provisioning/build/proof artifacts rather than claiming local preflight as CI. |
| 6 | Rehearse the existing ordinary-project build/cook/package instructions in an isolated fresh project. | Finds reusable SDK and authoring defects with a real consumer. Fix only demonstrated missing steps/API/resource diagnostics; finish with a runnable packaged public-API client. |
| 7 | Complete visible gameplay and physical input/audio acceptance when available; sample missing Jolt/GPU lifecycle counts. | Completes product evidence and exposes ownership defects. Use the existing route/reload harness, record live resource baselines and outcomes; hardware checks remain explicitly open until performed. |

Proceed through ready tasks in order. When an external acceptance item cannot run,
record its exact prerequisite and continue the next independent ready item. Do not
add another benchmark or subsystem wrapper without a failing workload or named
consumer. Separate-machine packaging, signing and legal review remain open.

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

Rechecked `../Elisa-compiler/STYLE_GUIDE.md` at `665f40d7` on 2026-10-07;
the compiler checkout and guide revision are unchanged.
The loop, region and strict-lint guidance is already in the main plan. Apply
these additional checks within implementation slices:

- At mutable-reference call sites, check aliases through reference locals,
  returned and conditional references, containers and function values. Use
  disjoint fields or scalar inputs with returned results. Review Unsafe aliases
  explicitly because compiler borrow exclusivity does not inspect them.
- For the input proof repair, preserve scalar snapshot bounds at copy time.
  An immutable copy must keep its own bound after record mutation; equality to
  the record's current field must expire. Immediate pure indexed writes now
  replay authenticated field copies; later uses across mutation remain open.
  The two scoped `device_index` result bindings in `apply` now authenticate
  their source calls inside value-block initializers. Keep value
  blocks and loop results; preserve shadowing and mutation refusals in replay.
- Scope side-effect temporaries only after checking allocation escape and live
  views. Use block initializers or helpers when values need to escape; use a
  helper when intervening loop jumps prevent nesting.
- Adopt strict lint as a diagnostic on touched code, then enable errors only
  for a clean directory. Preserve loop zero-iteration results and refuse forced
  rewrites where strict diagnostics explain why no valid rewrite exists.
- Prefer tuple yields when a touched loop produces several results and optional
  search results initialized to null when absence is part of the API. Use
  explicit `->` yields; bare loops in value position are invalid. Container
  value-threading remains design-only and must not become a prerequisite.

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
