# Course replay digests per subsystem

- `examples/character_course/replay_test.inc` records a `ReplaySubsystems`
  trace from the running course. Each of 16 fixed ticks records:
  - the input command (subsystem 0, Exact)
  - the tick (world, subsystem 1)
  - a millimetre digest of the character pose from Jolt (physics,
    subsystem 2, SameBuild)
- Course self-test codes:
  - 185: two runs with the same inputs, each with a fresh character at the
    free walker spawn, match in one build.
  - 186: a sidestep on tick 5 is named as an input divergence at tick 5, both
    within one build and across builds.
- The first attempt spawned at the course start, where the main character
  stands. The runs then disturbed each other and failed with 185, so a
  shared-world side effect breaks replay exactly as it should.
- Control: recording the input under subsystem 3 fails with 186. Both course
  smokes pass.
- Remaining for W09: recording digests over live play rather than a scripted
  run, persisting per-subsystem traces, and animation and AI digests.

## Saved traces (2026-09-30)

- The course saves its recorded trace under `course-replay-trace` through
  `UserData::write_payload`, using `ReplaySubsystemsCodec`, and reloads it
  with full validation.
- Code 187: the save succeeds, the reloaded trace decodes and matches a fresh
  same-input run, and after `remove_payload` it no longer loads.
- Control: comparing the reload against the sidestep run fails with 187.
  Both course smokes pass.

## AI and animation slots (2026-09-30)

The course replay now fills all five compared slots each tick:

- AI (3): an `AiBrain` watcher standing 4 m behind the spawn.
- Animation (4): an idle/walk `AnimGraph` driven by the character's
  measured speed.

Code 188 has two checks:

- With the watcher's sight cut to 0.5 m, the first difference is in AI,
  even when builds are compared.
- A 600 ms walk blend first differs in animation. A cross-build comparison
  ignores that difference, because animation is SameBuild scope.

Both course smokes pass. Negative control: recording a constant in the AI
slot makes the course smoke fail with 188.
