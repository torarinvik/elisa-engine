# Physics-to-Wicked render cadence validation

`test/physics_render_capture_native.elisa` drives one generation-checked Jolt
body through the public Elisa physics API and publishes its sampled pose into a
Wicked `RenderScene` instance. The smoke runs the same one-second simulation at
30 presentation frames per simulated second (two 60 Hz commits per frame) and
120 presentation frames per simulated second (one 60 Hz commit every other
frame). The smoke intentionally advances by frame count instead of pacing on
wall-clock time. It checks that each run ends on committed tick 60, that the
body has settled onto the floor, and that every `Application::pump()` leaves
the managed tick and body pose unchanged.

The smoke pumps initial render frames, then calls the public
`RenderScene::wait_for_pipelines` API so Wicked finishes asynchronous shader
compilation before either capture. Frames at physics ticks 30 and 60 are saved
from Wicked's SDL3/Metal backbuffer. The validation decodes each PNG, checks
that it contains visible pixels, and compares both PNG bytes and decoded pixels
between the 30 Hz and 120 Hz presentation runs. On the validated macOS host,
both pairs are 640x480 RGBA PNGs and are identical. The smoke stores them at:

- `build/validation/physics-render-cadence/physics-30hz-mid.png`
- `build/validation/physics-render-cadence/physics-120hz-mid.png`
- `build/validation/physics-render-cadence/physics-30hz.png`
- `build/validation/physics-render-cadence/physics-120hz.png`

Validation command from the engine root:

```sh
python3 scripts/application_native_smoke.py
```

The dedicated Jolt/Wicked cadence client passed on SDL3/Metal. It checks
matching midpoint and final renders at equal physics ticks, plus render-owned
step rejection. The aggregate `scripts/application_native_smoke.py` currently
stops earlier: `RuntimeServicesAudioProbe` returns status 185 because its
silent-audio voice count is nonzero. The standalone cadence client therefore
provides the native rendering evidence while that separate audio assertion is
unresolved. This does not compare every intermediate frame or exercise the full
game-session clock-to-hierarchy path.
