# Authored animation cross-fade validation

The RenderScene native smoke cooks a skinned glTF fixture with two distinct
translation clips: `lift` moves the animated joint from Y=1 to Y=2, while
`drop` moves it from Y=3 to Y=5. The Elisa test switches between them at a
shared normalized phase and reads the joint-local transform back from Wicked.

At phase 0.35, the eased transition checks the smoothstep weight 0.15625 and
the resulting joint translation Y=1.7171875. A second phase-matched linear
transition checks weight 0.25 and Y=1.9375. The test also verifies the initial
pose, animation cadence changes, same-clip continuation, and fade completion.

Run the complete SDL3/Metal gate on macOS with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

The full gate cooks all fixtures, runs native RenderScene coverage including
animation case group 270, and launches the packaged maze outside the checkout.
