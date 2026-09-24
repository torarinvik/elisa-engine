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

Frames at physics ticks 30 and 60 are saved from Wicked's SDL3/Metal backbuffer
and compared byte for byte between the 30 Hz and 120 Hz presentation runs. On
the validated macOS host, both pairs are 640x480 RGBA PNGs and are identical.
The client checks that pipeline waiting rejects an uninitialized render scene,
then waits for Wicked's background shader compilation to become idle before
capturing. The wait is bounded and reports a timeout through the public Elisa
error union.
The smoke stores them at:

- `build/validation/physics-render-cadence/physics-30hz-mid.png`
- `build/validation/physics-render-cadence/physics-120hz-mid.png`
- `build/validation/physics-render-cadence/physics-30hz.png`
- `build/validation/physics-render-cadence/physics-120hz.png`

Validation command from the engine root:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/application_native_smoke.py
```

The latest aggregate smoke passed all four native entries: primitive bodies,
this two-cadence Jolt/Wicked client, application lifecycle, and failure cleanup.
It validates the midpoint and final captures as PNGs and compares their decoded
RGBA pixels. This is native SDL3/Metal evidence for macOS. It does not compare
every intermediate frame or exercise the full game-session clock-to-hierarchy
path.
