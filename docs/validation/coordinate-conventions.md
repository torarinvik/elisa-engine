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
tests also use the same parity helper to derive winding reversal. This rule is
centralized and tested, but the current Wicked material shader path does not
consume it yet.

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

Validation on the pinned SDL3/Wicked Metal build:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 ~/.local/bin/elisascript scripts/wicked_probe.elisascript
```

Result: exit status 0; both native frame runs passed the coordinate probe,
scene topology and determinism checks, the asymmetric signed-scale reference
comparison, tangent-parity checks, and the frame-time budget. The Elisa check
suite passed `test/geometry.elisa` with the matching parity cases. This closes
the rendered-reference and centralized parity-math portions of F07. Tangent
parity still lacks a production mesh/material consumer, and physics, skinning,
and picking still need end-to-end signed-scale fixtures.
