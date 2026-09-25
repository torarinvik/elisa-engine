# Primitive and mesh physics shapes

**Status:** direct primitive bodies, shared primitive shapes, caller-array mesh
shapes, cooked-asset convex/triangle-mesh shapes, and reusable compound shapes
pass the focused SDL3/Metal native smoke on macOS. P02 remains open.

`PhysicsRuntime::BodyDesc` selects `BodyShape.Box`, `BodyShape.Sphere`, or
`BodyShape.Capsule`. Box dimensions are half-extents. Sphere uses `x` as its
radius. Capsule uses `x` as its radius and `y` as the half-height of its
straight cylindrical section. Unused sphere/capsule dimensions must be zero.
Each body descriptor also carries an initial `Geometry::Quat` orientation in
`(x, y, z, w)` order. Use `Geometry::quat_identity()` for an unrotated body.
`PhysicsRuntime::body_pose` and `RuntimeServices::physics_body_pose` return the
body's position and rotation after creation and simulation; `body_position`
remains the position-only convenience getter. The native boundary converts
both directions between Elisa's right-handed coordinates and Wicked's
left-handed coordinates. Primitive, reusable mesh, and compound-body creation
all apply the authored orientation. Non-finite or degenerate quaternions are
rejected before allocating a body slot; the primitive probe checks both direct
and shared-shape creation. A rotated-box raycast checks that the physics query
uses the authored orientation.
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
accepts static geometry packages and rejects skinned meshes. Packages with
animation clips, morph targets, or multiple scene placements are rejected until
the API can represent those deformations and transforms. It converts
positions and triangle winding at the Elisa-to-Wicked boundary, and each body
keeps a matching cooked-mesh scene-query proxy. Geometry is limited to 65,536
vertices and 196,608 indices per mesh, 8 MiB per shape, 32 MiB of shared shapes
per world, and 64 MiB of body proxies per world. Missing or unreadable packages
report `PhysicsError.AssetLoadFailed`; unsupported or degenerate geometry
reports `InvalidArgument`.

The collision-only format below bakes every supported static glTF scene
placement into the position stream. It therefore supports multi-placement
static scenes while retaining the runtime restriction against skinning,
animation, and morph deformation.

For runtime-generated or procedurally authored geometry, use
`PhysicsRuntime::shape_create_triangle_mesh` or
`shape_create_convex_hull` with indexed `Geometry::Vec3` arrays. Both
constructors validate and copy their inputs before returning, so callers may
reuse the arrays immediately. The same vertex, index, per-shape, per-world,
and query-proxy limits apply. `RuntimeServices::Session` exposes both
constructors as well.

For authored static glTF scenes, `scripts/cook_physics_collision.py` writes
collision-only geometry packages. It bakes the glTF node transforms, welds
identical positions, removes degenerate triangles, checks the native vertex,
index, coordinate, and memory limits, and can simplify the static mesh before
output. Choose `--shape convex_hull` or `--shape triangle_mesh`; the runtime
rejects using a package with the other shape kind. Raw `.collision` files and
`.elpk` bundles with a `collision` section are accepted by
`mesh_shape_create`. The cooked file contains positions and indices rather
than render attributes.

For example:

```sh
python3 scripts/cook_physics_collision.py assets/rock.gltf \
  --asset-path assets/rock.gltf --shape convex_hull \
  --output build/assets/rock-collision.elpk
```

The collision cooker self-test, native raw/ELPK collision reader, and C++
syntax check for the physics service passed on 2026-09-25. The SDL3 runtime
fixture now exercises both bundle shape kinds. That updated Elisa fixture is
pending execution because the refreshed stage1 compiler exits during native
compilation; the previous cooked-geometry runtime route had passed its native
smoke.

The runtime still asks Jolt to build its native shape from those bounded
streams when the asset loads. Versioned, pre-cooked Jolt shape serialization
and cache invalidation remain open P02 work.

For assemblies, use `PhysicsRuntime::shape_create_compound` with one to 16
`CompoundChild` entries, then attach the resulting handle through
`body_create_with_shape`. Each child entry supplies a reusable shape handle and
a local position and rotation. Nested compounds are supported. The compound
retains every child shape until it is destroyed, so child destruction reports
`PhysicsError.ShapeInUse` while a parent exists. A triangle mesh nested at any
depth keeps the compound static-only. Child transforms cross the same
right-handed Elisa to left-handed Wicked coordinate boundary as body poses.
The focused probe checks empty input, the child limit contract, nested shape
ownership, transformed child ray hits, the empty gap between children, and
cleanup order. `PhysicsRuntime::raycast` queries Jolt's collision shape, so a
broad render proxy cannot create a false hit through that gap.

Dynamic bodies expose checked linear-velocity read/write and impulse operations
through both `PhysicsRuntime` and `RuntimeServices`. Static and kinematic bodies
reject those operations. Before the first fixed step creates the native body,
they report `PhysicsError.BodyNotReady`. Velocity components must be finite and
no larger than 10,000 units/s; impulse components must be finite and no larger
than 10,000,000 units. The probes verify static-body rejection, velocity
set/get, impulse response, and the not-ready state.

`PhysicsRuntime::PhysicsMaterial(friction, restitution)` builds a bounded
per-body material value, and `body_set_material` / `physics_body_set_material`
apply it before or after body creation. Friction accepts finite values from 0
through 10; restitution accepts finite values from 0 through 1. The setter
updates Wicked's rigid-body component, which applies the values to Jolt during
the next fixed step. `test/physics_material_native_main.elisa` verifies the
RuntimeServices route and observes a dynamic sphere rebound from a static floor
with full restitution.

Each body descriptor also selects a collision category from
`0..<PhysicsRuntime::MAX_COLLISION_LAYERS` (32 categories). All category pairs
collide by default. `PhysicsRuntime::set_layer_collision` and
`RuntimeServices::physics_set_layer_collision` enable or disable a symmetric
pair for the managed world. Configure pairs before creating bodies; changing
the matrix while any body is live returns `PhysicsError.ConfigurationLocked`.
The Jolt filter assigns each body a private subgroup, so bodies in the same
category can still collide. A body's category also maps to the matching bit in
the existing query `layer_mask`: physical pair rules decide contact, while the
query mask independently decides which categories a physics ray or nearest
sphere/capsule-overlap query considers. All-hit sphere and capsule overlaps merge
unique managed Jolt-body hits with scene-query hits and honor the same category
mask. `test/physics_collision_layers_native.elisa`
verifies that a layer-1 body overlapping layer-0 bodies produces no contact,
that same-category bodies do contact, category-filtered physics rays and sphere
and capsule overlaps, invalid category indices, and the configuration lock.

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
dynamic triangle meshes are rejected, checks invalid scales and missing-package
errors, and raycasts against the cooked mesh. The RuntimeServices probe also
creates and releases both caller-array and cooked-asset shapes.

Validation on 2026-09-24:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools python3 scripts/application_native_smoke.py` passed all five entries, including primitive and cooked mesh shapes, render cadence, application lifecycle, and failure cleanup. The 30 Hz and 120 Hz midpoint/final captures match at 640x480.
- The run used the currently available stage1 compiler product with `ELISA_ALLOW_STALE_STAGE1=1`; it validates the engine changes against that product, not later unseeded compiler edits.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

Validation on 2026-09-25:

- `scripts/elisa_build_run.py run --main test/physics_primitives_native.elisa` passed with the current Elisa compiler, SDL3/Metal, and rebuilt Wicked/Jolt archives. The run covered shared primitives, compound child transforms and lifetime, accurate ray positions on both children, the gap miss, mesh shapes, capacity, and cleanup.
- `scripts/elisa_build_run.py run --main test/physics_app_native.elisa` passed. This isolated integration entry reuses `PhysicsAppProbe` to verify nearest/all-hit rays, sphere/capsule casts and overlaps, contact delivery, X-axis velocity and impulse conversion, and fixed-step ownership. The application smoke runner now includes it as a separate entry.
- The run used a temporary compiler wrapper to link the compiler's matching core runtime object, plus `DEVELOPER_DIR=/Library/Developer/CommandLineTools` for the post-upgrade linker. Neither workaround changes project or system settings.

Validation on 2026-09-25:

- Rebuilt the SDL3/Jolt Wicked archive with `cmake --build ../WickedEngine/build-elisa-sdl3 --target WickedEngine_ext_shaders -j 8`.
- The focused `test/physics_collision_layers_native.elisa` runner passed on SDL3/Metal. It overlaps a layer-1 dynamic body with two layer-0 bodies and confirms the layer-1 entity is absent from contact events while the same-category pair still contacts. Physics nearest/all-hit rays and nearest sphere/capsule overlaps select only the requested category; all-hit results preserve distance order, and the capsule cast reaches the selected body. Invalid categories and post-creation configuration changes are rejected.
- Sphere and capsule all-hit overlaps now also include managed Jolt bodies. The focused collision-layer fixture checks mask misses, exactly one selected-category hit, and consistent bounded-buffer counts for both shapes.
- The interactive maze now builds Jolt wall bodies and a category-1 key sensor. Gameplay uses a wall-layer sphere cast to gate each move and a category-filtered all-hit sphere overlap to collect the key. Its hidden SDL3/Metal self-test proves an added obstacle in an otherwise open cell blocks movement, removes the obstacle, and completes the key-and-door route; it passed both directly and through `scripts/render_scene_native_smoke.py`, including the packaged run with checkout access denied.
- `test/application_native_main.elisa` built and linked with native test probes enabled, compiling the RuntimeServices layer-configuration wrapper.
- `test/physics_material_native_main.elisa` passed on SDL3/Metal. It rejects restitution above 1, sets materials after Jolt body creation, and observes a sphere rebound from a full-restitution floor through the RuntimeServices API.
- The source-length, module-hygiene, and Python syntax checks passed.

`PhysicsRuntime::BodyInertia` validates positive finite principal moments up to
1e12 kg·unit² and a finite, non-degenerate principal-axis rotation. Set
`body_set_inertia` or `RuntimeServices::physics_body_set_inertia` on a dynamic
body before its first fixed step; later changes return `ConfigurationLocked`.
The getter reads Jolt's equivalent principal moments and body-local frame after
creation. Jolt may reorder the moments during eigendecomposition, so the probe
compares the reconstructed tensor rather than relying on input ordering.

`BodyDesc.center_of_mass_offset` and
`BodyInstanceDesc.center_of_mass_offset` shift the shape's computed center of
mass in body-local coordinates. Each component must be finite and within 10,000
physics units. The native adapter converts the vector across the
Elisa/Wicked handedness and unit boundary. Wicked wraps the scaled instance
shape, so reusable shapes can have different offsets per body without copying
the shared base shape. The entity pose, `BodyPose`, scene query proxy, and
physics query geometry continue to use the shape origin. Jolt shifts the mass
properties with the offset, including the corresponding parallel-axis inertia.

Validation on 2026-09-25:

- Wicked commits `5f5b43f` and `23ecbb9` add versioned rigid-body component
  fields and pass the rotated principal tensor into Jolt's
  `MassAndInertiaProvided` path. The
  SDL3 archive rebuilt with `cmake --build build-elisa-sdl3 --target
  WickedEngine_common -j8` and `cmake --build build-elisa-sdl3 --target
  WickedEngine_ext_shaders -j8`; `nm` confirmed the new adapter symbol.
- `scripts/elisa_build_run.py run --project . --main
  test/physics_inertia_native_main.elisa --output
  build/physics-inertia-native-smoke --native-test-probes` passed on
  SDL3/Metal. It checks invalid moments and rotations, `BodyNotReady` before
  creation, tensor-equivalent readback after Jolt reorders the 8/4/2 principal
  moments, and `ConfigurationLocked` on a late update.
- The same probe now verifies the authored body quaternion immediately after
  creation and after a fixed simulation step, using a non-spherical box so its
  orientation is physically observable. It passes with rotated inertia through
  the `RuntimeServices` pose getter.
- `test/physics_primitives_native.elisa` passes with regressions for degenerate
  direct and shared-shape quaternions and a ray through a rotated box. The ray
  would miss if the native body ignored its authored orientation.
- The complete eleven-fixture `scripts/application_native_smoke.py` run passed
  against Wicked commit `23ecbb9` after adding `physics-inertia-smoke`; that
  fixture round-trips a rotated principal frame through Jolt. Its 30 Hz and
  120 Hz render captures match pixel-for-pixel at 640x480.
- The source-length, module-hygiene, and whitespace checks passed.
- `BodyDesc` and `BodyInstanceDesc` center-of-mass offsets are validated on the
  focused probes. The direct box fixture checks the expected inertia trace and
  initial shape-origin pose; two bodies sharing a sphere shape verify separate
  mass properties and matching fall positions. An out-of-range offset is
  rejected before body creation. Both fixtures passed on SDL3/Metal.

The native registry supports reusable box, sphere, capsule, convex-hull,
triangle-mesh, and compound shapes (up to 16 children per compound, including
nested compounds), a bounded 32-category physical collision matrix, and
per-body friction/restitution and rotated principal inertia tensors.
Versioned, pre-cooked Jolt shape serialization remains open P02 work.
