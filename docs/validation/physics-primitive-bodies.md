# Primitive and cooked mesh physics shapes

**Status:** direct box, sphere, capsule, reusable primitive handles, and
runtime-cooked triangle-mesh/convex-hull handles pass the SDL3/Metal native
smoke on macOS. P02 remains open.

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

`PhysicsRuntime::shape_create_triangle_mesh` and
`shape_create_convex_hull` cook indexed `Geometry::Vec3` arrays into reusable
shapes. Both constructors copy their input before returning, so callers may
reuse the arrays immediately. Positions must be finite and within 10,000 units;
indices must form nondegenerate triangles and stay within the vertex array.
Each shape accepts up to 65,536 vertices and 196,608 indices, and a physics
world holds at most 32 MiB of retained shape geometry plus 64 MiB of estimated
per-body query-proxy data. Triangle meshes can only attach to static bodies.
Convex hulls can attach to static, kinematic, and dynamic bodies.
`RuntimeServices::Session` exposes both constructors and the same
generation-checked shape lifetime operations. Cooking happens at runtime;
offline collision cook packages and compound shapes remain open.

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
It also rejects out-of-range mesh indices and dynamic triangle meshes, changes
the caller's arrays after cooking to prove the native shape owns a copy, then
steps a convex hull onto a static triangle mesh and checks the settled height.
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
full SDL3/Metal gate passed all four clients with the new saturation assertion.
The compiler product was seeded before a later edit to
`src/semantic/check_destroyed_region.elisa`; that checker change was outside the
engine runtime test.

Mesh-shape follow-up on 2026-09-24: the full gate passed all four clients after
adding triangle-mesh and convex-hull cooking, mesh proxy accounting, and the
RuntimeServices routes. Exact 30 Hz/120 Hz midpoint and final capture pairs
still match at 640x480. The command was:

```sh
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/opt/homebrew/bin/python3 scripts/application_native_smoke.py
```

`src/physics/runtime.elisa` and `src/runtime/services.elisa` also compiled to
objects, and source-length, module-hygiene, and diff checks passed. The run used
the available stage1 compiler product with `ELISA_ALLOW_STALE_STAGE1=1`; it
validates the engine changes against that product, not later unseeded edits in
the adjacent compiler checkout.

P02 remains partial: native compound shapes, broadphase layers, and custom mass
properties are open. Mesh cooking is runtime-only and is not an offline
collision cook artifact.
