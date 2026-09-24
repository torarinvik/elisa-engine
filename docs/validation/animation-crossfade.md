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
weight of 0.25 toward 1.0. Both clips keep advancing during the fade, so the
displayed weight reaches 0.4875 before interruption back toward zero. It stays
at 0.4875 at the switch, reaches 0.31875 halfway through the retained
0.3-second recovery, and reaches 0.3 when the recovery ends. The non-animated
sibling remains at its 0.2 default throughout. Both joint and morph mixtures
therefore have interruption-continuity coverage.

Run the complete SDL3/Metal gate on macOS with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

The full gate cooks all fixtures, runs native RenderScene coverage including
animation case group 270, and launches the packaged maze outside the checkout.
