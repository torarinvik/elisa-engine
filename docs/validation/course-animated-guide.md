# Character course: animated guide

Delivery queue item 7 (C02/R08, N03) asks for playback and path services
connected to an independently moving character. Before this slice the course
guide walked Detour routes as a plain sphere, and the two skinned rigs played
their clip in place beside the posts. Now a third instance of the same
skinned package walks with the guide.

## Design

- **`walker.inc`** holds the policy in Elisa. `update_walker` places the rig
  at the guide character's feet and turns it toward the planar travel since
  the previous update. `facing` builds that yaw from the normalized direction
  without trigonometry. A move shorter than 1 mm keeps the previous heading.
- **Distance-locked playback.** The looping `lift` clip advances by
  `travelled / STRIDE` cycles (1.25 m per cycle), and only while the nav agent
  is `Moving`. An arrived or stranded guide holds its pose even when physics
  pushes it, and a slower guide takes slower strides. The engine only plays
  the clip (`advance_animation`) and places the instance
  (`update_transform`), so no native code decides animation state.
- **Play loop.** `optional_walker` creates the rig at the guide's pose. If
  the package is missing, play continues with the plain body. Each frame
  passes `NavAgent::state(&guide_agent) == Moving` to `update_walker`, and
  the rig is released before the navmesh unloads.
- The two posted rigs keep their own clocks (codes 114–118), so three
  instances of one package now run independent playback, and one of them
  follows independent route state.

## Checks

`walker_test` spawns its own character on the open floor at z = 5.5 and walks
it 6 m east from post A's x to post B's x, so the expected distance and
heading are known.

| Code | Failure |
|---|---|
| 131 | `facing` does not turn +Z to east, south or the diagonal, or a tiny move does not keep the previous heading |
| 132 | the walker rig does not load, or it does not add exactly one render instance |
| 133 | the agent does not arrive, or the cycles played are more than 0.15 away from the walked distance / stride |
| 134 | the rig's native clip progress is not the fractional part of the cycles played |
| 135 | after walking east, the rig does not face east |
| 136 | pushed 60 ticks north while not walking, the clip progress changes or the rig does not follow |
| 137 | releasing leaves the rig loaded, leaves the instance count raised, or its handle still answers |

## Commands

```
bash $SP/dbg.sh     # course self-test under lldb (character_course self_test_main)
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

On 2026-09-28 the course self-test exited 0 under lldb, and the course and
relaunch smokes and the native gate each exited 0. The first `check` run
exited 126; the rerun exited 0.

## Negative controls

- Advancing the clip whether or not the agent is walking fails at 136.
- Leaving the heading unchanged fails at 135.

Both edits were reverted, and `walker.inc` matches the verified copy.

## Gaps

- The rig is the synthetic two-joint panel and its `lift` clip, not a walk
  cycle. Stride locking is exercised, but there is no authored locomotion
  asset, and foot contact is not checked.
- Clip sampling still uses the engine's fixed-rate sampler. Production ozz
  contexts are not integrated or measured (C02).
- Each rig is a separate `create_mesh` instance. Shared animated clones and
  per-frame Elisa-sampled pose submission (R08) remain open.
- Nobody has yet watched the walker in manual play.
