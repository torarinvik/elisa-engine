# Authored animation cross-fade validation

The RenderScene native smoke cooks a skinned glTF fixture with two distinct
translation clips: `lift` moves the animated joint from Y=1 to Y=2, while
`drop` moves it from Y=3 to Y=5. The Elisa test switches between them at a
shared normalized phase and reads the joint-local transform back from Wicked.

At phase 0.35, the eased transition checks the smoothstep weight 0.15625 and
the resulting joint translation Y=1.7171875. A second phase-matched linear
transition checks weight 0.25 and Y=1.9375. The test also verifies the initial
pose, animation cadence changes, same-clip continuation, and fade completion.
It then interrupts the linear transition with a shorter fade: the first pose
stays at Y=1.9375, the interrupted pose is still at Y=1.54375 after 0.15
seconds, and the original remaining 0.3 seconds are retained until the new
destination is reached. A separate morph-only fixture cross-fades from a
weight of 0.25 toward 1.0, interrupts back toward zero, verifies that the
displayed value is 0.4875 at the first blend sample and remains 0.4875 at the
interruption. Both source and destination clips continue advancing during a
blend; halfway through the 0.3-second recovery the value is 0.31875, and the
destination clip reaches 0.3 when the fade completes. The non-animated sibling
remains at its 0.2 default throughout. Both joint and morph mixtures therefore
have interruption-continuity coverage.

Run the complete SDL3/Metal gate on macOS with:

```sh
ELISA_RUNTIME_OBJ="../elisa-compiler/build/runtime/elisacore_runtime.o" \
WICKED_BUILD="../WickedEngine/build-elisa-sdl3-homebrew" \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

The full gate cooks all fixtures, runs native RenderScene coverage including
animation case group 270, and launches the packaged maze outside the checkout.

## Explicit start phase

`RenderScene::play_animation_at` starts a non-looping clip at normalized phase
0 through 1, with bounded signed speed and an eased transition. It was pulled
from the Labyrinth worktree on 2026-09-26. A zero blend applies the requested
pose immediately; a positive blend retains the existing outgoing pose logic.
This starts playback at a phase rather than seeking an already-running clip.

Case group 270 now checks a start at phase 0.75 against joint Y=1.75, forward
clamping at phase 1, reverse playback from phase 0.25 clamping at phase 0,
rejection of phases outside the range without changing progress, and a paused
clip at phase 1. Existing tests continue to cover eased crossfades and
interrupted pose continuity.

Validation on 2026-09-26: the fixture-cooking stage passed, and the native
scene gate passed with `ELISA_RENDER_SCENE_RENDER_ONLY=1` using those fixtures,
the explicit runtime object above, and the optimized Homebrew Wicked build.
All existing visual comparisons also passed. The smoke bridge disables RTTI
because it uses none and must link with Wicked's RTTI-disabled build.

The runner now discovers the runtime inside an installed Stage1 snapshot by
reading its generated launcher's literal target. The explicit runtime override
in the recorded command is optional for that installation layout; explicit
CLI/environment overrides still take precedence. Discovery does not execute
wrapper commands or fall back to a different runtime when the snapshot's
runtime is missing. Focused tests cover snapshot selection, missing-runtime
rejection, and wrappers containing additional commands.
