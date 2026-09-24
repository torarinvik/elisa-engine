# Primitive physics bodies

**Status:** implemented and native-tested on macOS with SDL3/Metal and the pinned Wicked/Jolt build. P02 remains open.

`PhysicsRuntime::BodyDesc` now selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box dimensions are half-extents; sphere dimensions use `x`
as the radius; capsule dimensions use `x` as the radius and `y` as the half-height
of its cylindrical section. Sphere and capsule require the unused dimensions to
be zero. The native ABI validates these forms and configures Wicked's rigid-body
component without exposing Wicked or Jolt types to Elisa.

The adapter scales unit backend shapes to the requested dimensions. It also calls
`TransformComponent::UpdateTransform()` after submitting the initial pose, because
Wicked runs its physics update before its transform-update pass. Without this,
new bodies entered their first Jolt step at the identity world pose.

`test/physics_primitives_probe.elisa` rejects a zero-radius sphere, creates falling
sphere and capsule bodies, advances nine fixed ticks, checks that both moved under
gravity, and destroys them.
The probe is included in `test/application_native_main.elisa`; that application
passed its physics checks and later returned 185 from the separate
`RuntimeServicesAudioProbe` silent-voice assertion, so the aggregate
`scripts/application_native_smoke.py` run is not green yet.

The dedicated `test/physics_render_capture_native.elisa` run passed after the
shape-size and initial-pose changes. Its 30 Hz and 120 Hz captures were valid
640x480 PNGs and pixel-identical. The test used:

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
