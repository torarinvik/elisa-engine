# Typed Jolt primitive bodies

**Status:** box, sphere, and capsule creation passed the SDL3/Metal native
smokes on macOS. P02 remains open.

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

Validation:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/elisa_build_run.py run --project build/physics-smoke --main "$PWD/test/physics_primitives_native.elisa" --output "$PWD/build/physics-primitives-smoke" --wicked-build ../WickedEngine/build-elisa-sdl3 --native-test-probes` exited 0.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/application_native_smoke.py` ran the primitive smoke and the render-cadence smoke successfully. Both midpoint PNGs and both final PNGs matched byte-for-byte at 640x480. Artifacts: `build/validation/physics-render-cadence/physics-30hz-mid.png`, `physics-120hz-mid.png`, `physics-30hz.png`, and `physics-120hz.png`.
- The aggregate smoke then stopped in `application-native-smoke` with exit code 185 at `RuntimeServicesAudioProbe::unavailable_default_device`: its silent-fallback route observed a nonzero active-voice count. This is after the focused primitive and cadence clients passed. The audio fallback failure remains open and should be investigated separately.

This slice does not add reusable native shape handles, mesh or compound
cooking, collision layers, or mass-property controls.
