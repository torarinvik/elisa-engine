# Primitive and mesh physics shapes

**Status:** direct primitive bodies, shared primitive shapes, caller-array mesh
shapes, and cooked-asset convex/triangle-mesh shapes pass the SDL3/Metal native
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

For imported collision geometry, `PhysicsRuntime::mesh_shape_create` and
`RuntimeServices::physics_mesh_shape_create` accept a project-relative cooked
mesh package, a `MeshShape.ConvexHull` or `MeshShape.TriangleMesh` kind, and a
positive scale. The engine uses the bounded cooked-geometry loader already
used by rendering, verifies ELPK bundle dependencies, cooks the Jolt shape once,
and lets multiple bodies share it. A triangle mesh may only be attached to a
static body; convex hulls can be static, kinematic, or dynamic. This first API
accepts static geometry packages and rejects skinned meshes. It converts
positions and triangle winding at the Elisa-to-Wicked boundary, and each body
keeps a matching cooked-mesh scene-query proxy. Geometry is limited to 65,536
vertices and 196,608 indices per mesh, 8 MiB per shape, 32 MiB of shared shapes
per world, and 64 MiB of body proxies per world. Missing or unreadable packages
report `PhysicsError.AssetLoadFailed`; unsupported or degenerate geometry
reports `InvalidArgument`.

For runtime-generated or procedurally authored geometry, use
`PhysicsRuntime::shape_create_triangle_mesh` or
`shape_create_convex_hull` with indexed `Geometry::Vec3` arrays. Both
constructors validate and copy their inputs before returning, so callers may
reuse the arrays immediately. The same vertex, index, per-shape, per-world,
and query-proxy limits apply. `RuntimeServices::Session` exposes both
constructors as well. Cooking happens at runtime; offline collision cook
packages and compound shapes remain open work.

Dynamic bodies expose checked linear-velocity read/write and impulse operations
through both `PhysicsRuntime` and `RuntimeServices`. Static and kinematic bodies
reject those operations. Before the first fixed step creates the native body,
they report `PhysicsError.BodyNotReady`. Velocity components must be finite and
no larger than 10,000 units/s; impulse components must be finite and no larger
than 10,000,000 units. The probes verify static-body rejection, velocity
set/get, impulse response, and the not-ready state.

`test/physics_primitives_probe.elisa` verifies invalid dimensions, falling
sphere/capsule/zero-cylinder bodies, two bodies sharing one sphere shape,
in-use shape destruction rejection, final release, stale-handle rejection, and
native body-table exhaustion after filling all 64 slots. The host then shuts
down the saturated world, creates a fresh world, and successfully creates and
destroys another body. The application probe verifies velocity and impulse
behavior; the session probe exercises the public service routes.
The array-mesh probe also rejects out-of-range indices and dynamic triangle
meshes, mutates caller arrays after cooking to verify native copy ownership,
then steps a convex hull onto a static triangle mesh and checks its settled
height. It fills and releases the fixed 64-body registry and verifies that the
next creation returns `PhysicsError.Capacity`.
`test/physics_mesh_shapes_native.elisa` loads a tetrahedron package through the
Elisa API, creates a dynamic convex hull and static triangle mesh, verifies that
dynamic triangle meshes are rejected, and raycasts against the cooked mesh.

Validation on 2026-09-24:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` passed all five entries, including primitive and cooked mesh shapes, render cadence, application lifecycle, and failure cleanup. The 30 Hz and 120 Hz midpoint/final captures match at 640x480.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

The native registry now supports reusable box, sphere, capsule, convex-hull, and
triangle-mesh shapes. Compound shapes, broadphase layers, custom mass
properties, and offline collision cooking remain open P02 work.
