# Animation graph

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is C03 progress.

## Design

`src/animation/graph.elisa` (`AnimGraph`) is an Elisa-owned state machine of
up to 8 states and 16 transitions over 4 integer parameters.

- `validate` reports `NoStates`, `BadEntry`, `MissingNode` (a transition
  names a state that does not exist), `BadParam`, and `InstantCycle`: a loop
  of zero-length transitions that would spin forever inside one tick.
- `step` advances the blend and takes the first enabled transition out of
  the current state, at most one per step.
- A transition taken while a blend is still running is counted as an
  interruption. The new state starts at 1000 minus the weight the old
  blend had reached, so the pose does not pop back to the start.
- `weight` gives the current state's per-mille blend weight.

## Checks

`test/animation_graph.elisa` exits 0. It runs an idle/walk/run/attack graph:
no transition at idle, walk blending to 500 at half time, run interrupting
at 500 and finishing at 750 then 1000, a completed blend that is not counted
as an interruption, and each validation failure. Negative control: starting
interrupted blends from 0 makes the test exit 6.

## Gaps

No blend trees, sampling integration or authored graph asset yet, and the
graph is not wired into `Anim`, so C03 stays open.
