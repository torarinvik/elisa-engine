# Animation replay digest

`src/animation/graph_digest.elisa` (`AnimGraphDigest`) folds an `AnimGraph`
runtime into one digest for ReplaySubsystems slot 4 (animation). It covers
the current and previous state, blend time and length, the start weight and
the interruption count.

`test/animation_graph_replay.elisa` (in the gate's unit-test list) drives an
idle/walk/run graph for 14 ticks with a scripted speed. It records input in
slot 0 and the graph digest in slot 4. It checks that:

- two identical runs match;
- a longer walk blend with the same input diverges first in animation at
  tick 3, when the transition fires;
- a one-tick input spike is found in input first;
- across builds, animation (SameBuild scope) is not compared;
- the interruption count alone changes the digest.

Negative control: leaving the blend length out of the digest fails with
code 2.

Not covered: the character course does not record slot 4 yet, and AI (slot
3) digests remain.
