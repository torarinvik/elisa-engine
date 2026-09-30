# AI replay digest

`src/ai/brain_digest.elisa` (`AiBrainDigest`) folds an `AiBrain::Brain` into
a per-tick digest for ReplaySubsystems slot 3 (AI, Exact scope). It covers
the brain's transition digest, the current and previous mode, ticks in mode,
the transition count, the patrol waypoint and whether a last-seen point is
held. Positions (f32) are left to the world and physics slots.

`test/ai_brain_replay.elisa` (in the gate's unit-test list) has a target walk
toward a still agent for 16 ticks. It checks that:

- two runs match;
- with 8 m sight instead of 12 m, the first difference is in AI at tick 4,
  when the 12 m sighting is lost;
- AI is compared across builds;
- a target that jumps at tick 2 is found in input first;
- time in mode alone changes the digest.

Negative control: leaving ticks in mode out of the digest fails with code 5.

Not covered: the character course has no AI agent recording slot 3 yet.
