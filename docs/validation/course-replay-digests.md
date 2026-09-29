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
