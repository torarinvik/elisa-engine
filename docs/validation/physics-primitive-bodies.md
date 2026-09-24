# Primitive physics bodies

**Status:** implemented and native-tested on macOS with SDL3/Metal and the pinned Wicked/Jolt build. P02 remains open.

`PhysicsRuntime::BodyDesc` now selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box dimensions are half-extents; sphere dimensions use `x`
as the radius; capsule dimensions use `x` as the radius and `y` as the half-height
of its cylindrical section. Sphere and capsule require the unused dimensions to
be zero. The native ABI validates these forms and configures Wicked's rigid-body
component without exposing Wicked or Jolt types to Elisa.

The adapter scales unit backend shapes to the requested dimensions. It generates
a capsule mesh for Wicked's scene-query BVH using the same radius and cylinder
half-height as the Jolt collider; a zero-cylinder capsule uses sphere geometry
and a Jolt sphere because Jolt rejects a zero-height capsule. It also calls
`TransformComponent::UpdateTransform()` after submitting the initial pose, because
Wicked runs its physics update before its transform-update pass. Without this,
new bodies entered their first Jolt step at the identity world pose.

`test/physics_primitives_probe.elisa` rejects a zero-radius sphere, creates falling
sphere, capsule, and zero-cylinder capsule bodies, advances nine fixed ticks,
checks that each moved under gravity, and destroys them. The probe is included
in the application smoke and also runs under the standalone
`test/physics_primitives_native.elisa` host lifecycle.

`scripts/application_native_smoke.py` passes both the standalone primitive test
and the dedicated render cadence test. The latter checks valid, visible 640x480
captures at physics ticks 30 and 60; 30 Hz and 120 Hz PNG bytes and decoded
pixels match for both pairs. The runner later reaches the application-wide test,
which still exits 185 in `RuntimeServicesAudioProbe` because the silent-audio
voice count is nonzero.

The earlier direct capture command used:

```sh
ELISA_USER_DATA_DIR="$PWD/build/physics-shape-debug/user-data" \
ELISA_PHYSICS_30HZ_CAPTURE_PATH="$PWD/build/physics-shape-debug/physics-30hz.png" \
ELISA_PHYSICS_120HZ_CAPTURE_PATH="$PWD/build/physics-shape-debug/physics-120hz.png" \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/opt/homebrew/bin/python3 scripts/elisa_build_run.py run \
  --project build/physics-shape-debug --native-test-probes \
  --wicked-build ../WickedEngine/build-elisa-sdl3
```

The temporary project manifest pointed to `test/physics_render_capture_native.elisa`.
The generated PNGs are in the ignored `build/physics-shape-debug/` directory.
This slice does not add reusable native shape handles, mesh or compound shapes,
collision-layer selection, or mass-property controls.
