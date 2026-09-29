# Vehicle wheel policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is P09 progress. It is integer policy only; no physics engine is bound.

## Design

`src/physics/vehicle.elisa` (`PhysicsVehicle`), in mm, mm/s, N and permille:

- `suspension_force` is a spring plus damper that never goes negative, and is
  zero for an unloaded wheel. Inputs beyond ±1,000,000 give zero.
- `steer_permille` clips input to ±1000 and narrows lock linearly to a
  quarter at `full_speed_mm_s`. A non-positive full speed gives zero.
- `tyre_force` spends a friction budget (load × friction). Cornering is served
  first; drive gets what is left, and `saturated` reports over-demand.

## Checks

- `test/physics_vehicle.elisa` exits 0 (codes 1–15: spring, damper, rebound,
  unloaded, overflow guard, steering scale and clip, tyre budget, saturation,
  reverse drive, no load).
- Negative control: adding 1 N to the spring total makes it exit 1.
- Source-length check passes.

## Gaps

- No proof harness for this module.
- No solver, ground contact, anti-roll or drivetrain, and no Box2D or other
  physics binding; the full gate is still blocked at the `world-test` link.
