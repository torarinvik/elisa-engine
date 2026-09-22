# Coordinate convention validation

`native/coordinate_conventions.h`, `native/coordinate_abi.h`, and
`native/coordinate_transform_bridge.h` define the Elisa metre-sized
right-handed world (+Y up) and Wicked's reflected frame. The ABI normalizes
XYZW quaternions and applies the X-reflection basis change; affine 4x4 matrices
stay column-major at the service boundary and are transposed into Wicked's
row-vector layout at submission. Positions use the profile's metres-per-unit
scale, while signed nonzero scale is preserved. General affine payloads can be
converted for point operations; submission to Wicked's TRS-only component
rejects shear and singular scale before mutating the destination.

Coordinate ABI v2 stores tangent parity as a signed multiplier. Both the
backend-neutral `Geometry::tangent_parity_for_transform` function and native
ABI helper multiply the authored tangent W by the basis reflection and each
negative scale axis. Invalid source signs and singular/non-finite scales are
rejected (the Elisa math helper returns its documented zero sentinel). Native
tests also use the same parity helper to derive winding reversal. Wicked stores
the object's orientation sign in the existing `ShaderMeshInstance` padding,
derives it from the full world-transform determinant, and applies it when
building raster and ray-tracing tangent frames. The asymmetric signed-scale
reference uses a normal map, so its golden image exercises the production
material path.

The signed-scale physics fixture gives a dynamic box scale `(-0.5, 0.25, 1.5)`,
steps it through Jolt, and checks a real Jolt ray hits its center but misses
above its reduced Y extent. This initially exposed negative box half-extents
being passed to Jolt. Wicked commit `bf8945b` now applies absolute scale to
primitive collision dimensions (box, sphere, capsule, and cylinder); the
full SDL3/Wicked gate passes with the fix.

The skinned-quad fixture first receives positions already deformed by Elisa,
then submits a world transform with scale `(-2.0, 0.5, 1.5)` and translation
`(0.25, -0.5, 0.75)`. The adapter reflects the vertices into Wicked space and
uses the shared winding-parity rule, where the single negative scale axis
cancels the basis reflection. Elisa checks the four transformed corners, and
the native probe applies Wicked's actual world matrix and checks all four
positions against the same expected coordinates.

The native gate exercises `native/coordinate_probe.h` after the real scene,
physics, skin payload, and camera path have run. The fixture uses an asymmetric
point, camera ray, translation, and negative nonuniform scale. A second render
submits a rotated, negative nonuniform cuboid and three differently colored
markers through the transform bridge, then saves a separate reference image.
The committed reference is [`backends/coordinate_reference.png`](../../backends/coordinate_reference.png).
The gate requires exact pixels between the two runs and compares the rerun to
the committed reference with the screenshot comparator's default tolerance.
The coordinate probe output is:

```text
coordinates: point=(1.25,-2.50,3.75) scale_parity=1 ray=(-0.20,-0.10,-1.00)
```

The probe verifies the profile and payload ABI layouts, rejects depth mismatch,
zero quaternions, and projective matrices, then checks point, direction, and
ray round trips. An asymmetric affine matrix transforms a point identically
before and after the basis conversion; the quaternion rotation matrix agrees
with that same basis rule. A nonuniform signed-scale TRS payload and a
decomposable affine matrix are submitted to real Wicked transforms and read
back; the sheared matrix is rejected without changing the transform. The
profile's unit scale is exercised at 0.01 metres per Elisa unit. The shared
fixture also feeds render, physics, skin, and picking adapters; existing wall,
marker, route, physics, and skinned-quad paths consume the same conversion
functions.

The render-scene service didn't use these functions at first. It passed
Elisa coordinates to Wicked unreflected and swapped each cooked triangle's
winding, so every frame showed world +X on the left. It now reflects
transforms, the camera, cooked vertices and tangents, animation poses, the sun
and arc points through them, and its frames match Godot's. See
[`render-scene-handedness.md`](render-scene-handedness.md). Godot needs no
reflection, but it draws clockwise front faces, so its host reverses each
cooked triangle once in `backends/godot/cooked_mesh.gd`. See
[`godot-cooked-winding.md`](godot-cooked-winding.md).

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: exit status 0; both native frame runs passed the coordinate probe,
scene topology and determinism checks, the asymmetric signed-scale reference
comparison, tangent-parity checks, signed-scale ray-picking hit/miss checks,
signed-scale Jolt hit/miss checks, signed-scale skinned-vertex transform and
winding checks, and the frame-time budget. The Elisa check suite passed
`test/geometry.elisa` and `test/maze.elisa` with matching parity and transformed
skin-corner cases. F07 is complete across rendering, production tangent frames,
physics, skinning, picking, and transform conventions.
