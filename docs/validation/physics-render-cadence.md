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

The cadence client validates its midpoint and final captures as PNGs and
compares their decoded RGBA pixels. This is native SDL3/Metal evidence for
macOS. It does not compare every intermediate frame or exercise the full
game-session clock-to-hierarchy path.

`test/world_physics_pose_native_main.elisa` also checks error preservation in
`WorldPhysics`: it binds a dynamic body, shuts down and reopens the owning
session, then advances and synchronizes through both flat and hierarchy
bindings. Each stale body must surface as
`PhysicsRuntime::PhysicsError.StaleWorld`; the former generic
`WorldPhysicsError.SynchronizationFailure` result fails either check. Both
frame-advance routes now propagate their underlying physics errors. The
same client binds a dynamic body followed by a kinematic body, removes the
dynamic row, then checks that the kinematic target still reaches Jolt through
the session clock. It repeats target delivery through hierarchy bindings.
For the hierarchy route, it leaves the local target dirty, verifies the
read-only target composition does not publish it, advances one fixed tick,
then checks the half-alpha `WorldRendering` snapshot is between the old and
new physics poses while the authoritative hierarchy remains at the new pose.
The focused SDL3/Metal client built and ran successfully on macOS 27.

`test/world_hierarchy_render_native_main.elisa` separately takes that kind of
half-alpha hierarchy snapshot through `WorldRendering::extract_hierarchy` and
submits it to a live Wicked instance. It checks that the committed hierarchy
pose stays authoritative, that picking at the sampled pose resolves to the
same gameplay entity, and that the object visibly moves between two backbuffer
captures. The pipeline wait is followed by warm-up frames so the first image
contains the object. On the validated macOS host, the 320x200 hidden window
produces 640x400 backing-store PNGs; the smoke checks the two captures have
matching dimensions, contain visible pixels, and differ by at least 128 pixels.
Run only this client with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_NATIVE_SMOKE_ONLY=world-hierarchy-render-smoke \
python3 scripts/application_native_smoke.py
```

This focused client uses `elisa_build_run.py --no-public-runtime` because its
entry file includes each needed module explicitly. The default project runner
still compiles `src/runtime/public.elisa`. That full bundle currently reaches
four unrelated Stage1 C-archive declines in capability/provider-report
functions, so the session-clock version of the hierarchy-to-Wicked smoke is
blocked until that compiler backend gap is fixed. The focused render smoke
validates hierarchy extraction, live rendering, and picking independently of
that full runtime bundle.

The portable interpolation test also covers quaternion sign equivalence and
shortest-path blending across the ±180° boundary. Physics poses may use either
sign for the same quaternion, so the interpolation endpoint is sign-corrected
before normalized linear blending.
