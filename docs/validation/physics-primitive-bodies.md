# Primitive physics bodies and reusable shapes

**Status:** box, sphere, capsule, and shared primitive shape handles pass the
focused SDL3/Metal native smoke on macOS. P02 remains open.

`PhysicsRuntime::BodyDesc` selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box dimensions are half-extents. Sphere uses `x` as its
radius. Capsule uses `x` as its radius and `y` as the half-height of its straight
cylindrical section. Sphere and capsule require the unused dimensions to be zero.
The native ABI validates these forms without exposing Wicked or Jolt types to
Elisa. Zero-cylinder capsules use sphere geometry because Jolt rejects a
zero-height capsule.

For reusable shapes, call `PhysicsRuntime::shape_create` with a primitive kind
and dimensions, then create any number of bodies with
`PhysicsRuntime::body_create_with_shape` and a `BodyInstanceDesc`. The affine
`RuntimeServices::Session` has matching shape and body operations. Wicked reads
the shared, precomputed Jolt shape through its mesh component; every body still
has its own transform and scene-query proxy. `shape_destroy` returns
`PhysicsError.ShapeInUse` while any body references the shape. Destroying the
last body releases that reference, after which the shape may be destroyed.
World shutdown invalidates all remaining handles and releases their backend
resources.

The body adapter scales unit primitives for the direct `BodyDesc` path. For
capsules, it converts Elisa's half-height to Wicked's full cylinder-length
setting. Its bounded 16-segment, 8-step-per-hemisphere capsule mesh uses the same
radius and half-height. The adapter also primes each submitted transform before
the first physics update, because Wicked reads entity scale before its ordinary
transform-update pass.

`test/physics_primitives_probe.elisa` checks invalid sphere dimensions, direct
sphere/capsule/zero-cylinder motion, two bodies using one shared sphere shape,
shape-destruction rejection while referenced, successful release, and rejection
of a stale shape handle. `test/physics_primitives_native.elisa` runs this probe
through the hidden SDL3/Metal host lifecycle.

Validation on 2026-09-24:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/application_native_smoke.py` passed `physics-primitives-smoke` and `physics-render-capture-smoke`. Midpoint and final Jolt/Wicked captures match pixel-for-pixel at 640x480 between 30 Hz and 120 Hz presentation.
- The complete runner passed all four native entries: primitive bodies, render cadence, application lifecycle, and failure cleanup. The application fixture now starts the falling test body at y=0.7 so ordinary ground contact happens within the eight-step smoke window; the same test verifies the sensor contact and removal events.
- `PhysicsRuntime` and `RuntimeServices::Session` expose bounded linear-velocity reads/writes and impulse application for dynamic bodies. The native application probe checks rejecting velocity on a static body, reads a set velocity, applies an impulse, and observes the resulting velocity increase.
- Velocity and impulse calls return `PhysicsError.BodyNotReady` until the first fixed step has created the Wicked/Jolt body. The probe checks this explicitly; stale handles, non-dynamic bodies, and out-of-range/non-finite vectors use their own errors.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `python3 scripts/test_elisa_build_run.py` passed. The Python test command ran 13 tests.

The native shape registry currently supports reusable primitive shapes only. It
does not yet support cooked mesh or compound shapes, broadphase layers, or
custom mass properties.
