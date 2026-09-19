# Coordinate convention validation

`native/coordinate_conventions.h` is the single native conversion boundary for
the Elisa metre-sized right-handed world (+Y up) and Wicked's reflected frame.
It provides position and direction conversion in both directions, finite and
epsilon checks, asymmetric transform application, and the winding-parity rule
for the one handedness reflection plus negative scale determinants.

The native gate exercises `native/coordinate_probe.h` after the real scene,
physics, skin payload, and camera path have run. The fixture uses an asymmetric
point, camera ray, translation, and negative nonuniform scale. Its output on
2026-09-19 was:

```text
coordinates: point=(1.25,-2.50,3.75) scale_parity=1 ray=(-0.20,-0.10,-1.00)
```

The probe verifies position and direction round trips, finite values, negative
scale winding parity, and picking-ray origin/direction round trips. Existing
wall, marker, route, physics, and skinned-quad paths consume the same conversion
functions, so a second native sign convention cannot silently enter those
paths.

F07 remains open: matrix/quaternion ABI conversion, render/physics transform
ownership, and a captured asymmetric scene with ray-hit and skinning reference
images still need to become one reusable Elisa-facing transform service.
