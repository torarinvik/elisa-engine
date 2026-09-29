# 3D thin-wall projectiles

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler and the Wicked/Jolt backend. This is P08 progress. P08's done
criterion lists thin-wall projectiles; until now the 3D path had no test of
them.

## Setup

Wicked creates every Jolt body with `EMotionQuality::LinearCast`
(`wiPhysics_Jolt.cpp`, `cMotionQuality`). The engine does not change it, and
there is no per-body switch.

`test/physics_ccd3d_probe.elisa` runs as `physics-ccd3d-smoke` in
`scripts/application_native_smoke.py`.

- It fires a 5 cm sphere along +x at a static 2 cm wall that is 40 m square.
- Each shot runs 30 steps at 60 Hz and tracks the furthest x the centre
  reaches.
- Bodies join Jolt on the next step, so the probe steps once before
  launching.

## Measurements

| Speed | Distance per step | Wall thicknesses per step | Furthest centre x |
| --- | --- | --- | --- |
| 50 m/s | 0.83 m | 42 | −0.048 m |
| 600 m/s | 10 m | 500 | −0.048 m |

Contact would put the centre at −0.06 m, so Jolt lets the ball penetrate
12 mm. A sweep of 6–40 m/s and of 45–600 m/s never passed the wall's
midplane.

The probe asserts, at 50, 150, 300 and 600 m/s, that:

- the centre stays below −0.03 m (a 30 mm penetration budget);
- the ball actually reaches the wall (within 10 cm).

## Checks

- `scripts/application_native_smoke.py --only physics-ccd3d-smoke` passes.
- Negative control: moving the wall out of the path makes the smoke fail
  with status 10.
- CCD cannot be switched off from the engine without editing Wicked, so
  there is no control that shows a discrete body tunnelling.

## Gaps

- There is no per-body motion-quality setting, and there is no diagnostic
  for CCD hits.
- Only spheres against a box were tested. Thin meshes, rotating bodies and
  dynamic-versus-dynamic cases were not.
- 3D solver presets and timing remain open.
