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
compilation before either capture. Both final frames are saved from Wicked's
SDL3/Metal backbuffer, decoded, checked for visible pixels, and compared pixel
for pixel. On the validated macOS host, both are 640x480 RGBA PNGs and are
identical. The smoke stores them at:

- `build/validation/physics-render-cadence/physics-30hz.png`
- `build/validation/physics-render-cadence/physics-120hz.png`

Validation command from the engine root:

```sh
python3 scripts/application_native_smoke.py
```

The full application smoke passed, including the existing lifecycle and failure
cleanup clients plus the new two-cadence Jolt/Wicked client. This is native
SDL3/Metal evidence for macOS. It checks equal-duration final rendered output
and render-owned stepping, but does not yet prove pixel equivalence at every
intermediate frame or the clock-to-hierarchy path used by a full game session.
