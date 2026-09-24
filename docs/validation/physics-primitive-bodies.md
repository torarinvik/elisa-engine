# Typed Jolt primitive bodies

**Status:** box, sphere, and capsule creation, plus dynamic linear velocity and
impulse controls, passed the SDL3/Metal native smokes on macOS. P02 remains
open.

`PhysicsRuntime::BodyDesc` now selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box `dimensions` are half-extents. Sphere uses `x` as its
radius. Capsule uses `x` as its radius and `y` as the half-height of its
straight cylindrical section. Sphere and capsule reject nonzero unused fields.
The existing `elisa_physics_v1_create_box` C entry remains as a compatibility
wrapper for native clients.

The adapter maps unit Jolt shapes through the transform scale using Wicked's
actual Jolt integration rules. It builds matching scene-query geometry, primes
the submitted transform before the first physics update, and keeps native
Wicked/Jolt types behind the flat ABI. The capsule query mesh is a bounded
16-segment, 8-step-per-hemisphere proxy.

`test/physics_primitives_probe.elisa` rejects a zero-radius sphere, creates
falling sphere and capsule bodies, advances nine 30 Hz steps, checks both moved
under gravity, then destroys them. The standalone application entry is
`test/physics_primitives_native.elisa`.

Dynamic bodies also expose checked linear velocity read/write and impulse
operations through both `PhysicsRuntime` and the session-routed
`RuntimeServices` API. Static and kinematic bodies reject these operations;
the body must have entered the Jolt simulation at least once before velocity
can be read or changed. Vectors are required to be finite and are bounded to
10,000 units/s for velocity and 10,000,000 units per impulse component. The
physics application probe verifies static-body rejection, set/get behavior,
and an impulse changing velocity. The runtime-services probe exercises the
same controls through an open session.

Validation:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/elisa_build_run.py run --project build/physics-smoke --main "$PWD/test/physics_primitives_native.elisa" --output "$PWD/build/physics-primitives-smoke" --wicked-build ../WickedEngine/build-elisa-sdl3 --native-test-probes` exited 0.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` passed all four native entries: primitive bodies, render cadence, application lifecycle, and failure cleanup. Midpoint and final PNG pairs decode to identical 640x480 RGBA pixels. Artifacts: `build/validation/physics-render-cadence/physics-30hz-mid.png`, `physics-120hz-mid.png`, `physics-30hz.png`, and `physics-120hz.png`.
- During diagnosis, exit code 185 was traced to the application physics fixture: it required a ground-contact event after eight fixed steps, before the falling box had reached the ground. Starting that test body at y=0.7 gives the contact enough time to occur while it still overlaps the sensor. The full gate now reaches and passes the subsequent silent-audio fallback checks.

This slice does not add reusable native shape handles, mesh or compound
cooking, collision layers, or mass-property controls.
