# Targeted runtime delivery — 2026-10-07

This refines the active queue in `IMPLEMENTATION_PLAN.md`. The full backlog
remains open. Select a concrete defect or missing acceptance result in the
Character Course before promoting a subsystem-wide feature.

## Next executable slices

| Order | Deliverable | Acceptance and value |
| --- | --- | --- |
| 1 (automated complete) | Focus-loss recovery through the live course loop | Hold movement and crouch, lose focus, verify automatic pause and cleared actions, then resume and finish the existing route. Run through SDL event translation; record an OS window-switch separately. Prevents stuck movement and unintended gameplay after switching applications. |
| 2 (automated complete) | Resize the running course in play, pause and controls | Verify logical/drawable dimensions, camera aspect and readable HUD at small and large sizes. Reuse the existing native resize hook and retain captures. Fix clipping or stale layout exposed by the check. Physical DPI/display changes remain separate evidence. |
| 3 (automated complete) | One production worker-event consumer under `WorldSchedule` | Reuse Jolt's existing bounded contact queue. Resolve contact participants through checked body bindings before routing a crate impact to gameplay/audio. Reject stale world epochs and wrong access phases; retain overflow and unsubscribe evidence. Avoid constructing another public scheduler or generic queue before a producer needs it. |
| 4 (automated complete) | Measure the course's actual audio workload | Record voice/stream counts, memory and underruns during route, pause, reload and teardown. Exercise existing cancellation/device recovery rather than adding another audio feature. Physical listening and unplug/reconnect remain explicitly unverified until performed. |
| 5 | Cook and measure the pinned authored Cesium Man rig | Reuse Ozz's local glTF fixture (19 skin joints, one clip; retain its Cesium CC BY attribution). Exercise the normal geometry/keyed-contract cook and eight independent rendered instances. Target p99 <=1 ms per eight-instance CPU update and zero steady allocations; record package/source hashes and sampled memory. Fix an actual import or sampling defect before adding animation graph features. |
| 6 | Rebuild and relocate the updated game | Verify optimized clean provenance, resource hashes, offline startup and graceful teardown after the preceding runtime changes. Keep separate-machine testing, signing and legal review visible as external acceptance work. |

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
The next slice is cooking and measuring the pinned authored rig.
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
