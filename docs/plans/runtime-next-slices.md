# Targeted runtime delivery — 2026-10-07

This refines the active queue in `IMPLEMENTATION_PLAN.md`. The full backlog
remains open. Select a concrete defect or missing acceptance result in the
Character Course before promoting a subsystem-wide feature.

## Next executable slices

| Order | Deliverable | Acceptance and value |
| --- | --- | --- |
| 1 (automated complete) | Focus-loss recovery through the live course loop | Hold movement and crouch, lose focus, verify automatic pause and cleared actions, then resume and finish the existing route. Run through SDL event translation; record an OS window-switch separately. Prevents stuck movement and unintended gameplay after switching applications. |
| 2 | Resize the running course in play, pause and controls | Verify logical/drawable dimensions, camera aspect and readable HUD at small and large sizes. Reuse the existing native resize hook and retain captures. Fix clipping or stale layout exposed by the check. Physical DPI/display changes remain separate evidence. |
| 3 | One production worker-event consumer under `WorldSchedule` | Reuse Jolt's existing bounded contact queue. Resolve contact participants through checked body bindings before routing a crate impact to gameplay/audio. Reject stale world epochs and wrong access phases; retain overflow and unsubscribe evidence. Avoid constructing another public scheduler or generic queue before a producer needs it. |
| 4 | Measure the course's actual audio workload | Record voice/stream counts, memory and underruns during route, pause, reload and teardown. Exercise existing cancellation/device recovery rather than adding another audio feature. Physical listening and unplug/reconnect remain explicitly unverified until performed. |
| 5 | Validate keyed animation on representative content | Use the course's independent instances, then an authored rig with a stated joint/clip count. Record optimized CPU percentiles, allocations and sampled memory. Add transitions/root motion only when the game's motion exposes a concrete need. |
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
remains external acceptance. Resize/HUD behavior is the current slice. Paging policy passed its exhaustive
test and 20/20 proof obligations; real pause/play resize plus full traversal
passed (171.274s). Small controls captures and pointer checks remain open.
See [resize evidence](../validation/course-resize.md).

`66911c90` routes effect payload consumption through `WorldSchedule`, with live
entity validation under its read token. The native effect smoke and the
environmental-effects capture passed. This establishes a scheduled consumer;
native worker ingress and course contact ownership remain open.

See [course controls](../validation/course-controls.md),
[world events](../validation/world-events.md),
[physics queries](../validation/physics-queries.md) and
[Ozz animation](../validation/animation-ozz-service.md) for current results and
their limits.
