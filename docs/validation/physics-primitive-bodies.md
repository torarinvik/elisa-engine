# Primitive physics bodies and reusable shapes

**Status:** direct box, sphere, and capsule bodies and shared primitive-shape
handles pass the SDL3/Metal native smoke on macOS. P02 remains open.

`PhysicsRuntime::BodyDesc` selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box dimensions are half-extents. Sphere uses `x` as its
radius. Capsule uses `x` as its radius and `y` as the half-height of its
straight cylindrical section. Unused sphere/capsule dimensions must be zero.
The ABI keeps Wicked and Jolt types private. Zero-cylinder capsules map to
spheres because Jolt rejects zero-height capsules. Direct bodies scale unit
shapes and keep scene-query proxy geometry aligned with their physics shape.

For shared primitives, create a world-scoped `ShapeHandle` with
`PhysicsRuntime::shape_create`, then create bodies using
`body_create_with_shape` and `BodyInstanceDesc`. `RuntimeServices::Session`
exposes the corresponding affine service operations. Shapes are generation
checked; `shape_destroy` reports `PhysicsError.ShapeInUse` while any body uses
the shape. Destroy the bodies first, then the shape. Shutdown releases all
native shape resources and invalidates their handles. Each body retains its own
transform and scene-query proxy while sharing the precomputed Jolt shape.

Dynamic bodies expose checked linear-velocity read/write and impulse operations
through both `PhysicsRuntime` and `RuntimeServices`. Static and kinematic bodies
reject those operations. Before the first fixed step creates the native body,
they report `PhysicsError.BodyNotReady`. Velocity components must be finite and
no larger than 10,000 units/s; impulse components must be finite and no larger
than 10,000,000 units. The probes verify static-body rejection, velocity
set/get, impulse response, and the not-ready state.

`test/physics_primitives_probe.elisa` verifies invalid dimensions, falling
sphere/capsule/zero-cylinder bodies, two bodies sharing one sphere shape,
in-use shape destruction rejection, final release, and stale-handle rejection.
The standalone primitive client also fills the fixed 64-body registry, verifies
that the next body creation returns `PhysicsError.Capacity`, then releases every
body and the shared shape.
The application probe verifies velocity and impulse behavior; the session probe
exercises the public service routes.

Validation on 2026-09-24:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` passed all four entries: primitive bodies, render cadence, application lifecycle, and failure cleanup. The 30 Hz and 120 Hz midpoint/final captures match at 640x480.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

Capacity follow-up on 2026-09-24: after running
`bash scripts/elisac_stage1.sh --seed` in the adjacent compiler checkout, the
full SDL3/Metal gate passed all four clients with the new saturation assertion:
`ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/application_native_smoke.py`.
This run used `ELISA_ALLOW_STALE_STAGE1=1` and the freshly seeded compiler product because
`src/semantic/check_destroyed_region.elisa` was edited again after that seed;
the runtime test does not exercise that checker change. This validates the
engine change against the seed product, not the compiler checkout's later edit.
Source-length, module-hygiene, and diff checks passed as well.

The native registry currently supports reusable box, sphere, and capsule shapes.
It does not yet support cooked mesh or compound shapes, broadphase layers, or
custom mass properties.
