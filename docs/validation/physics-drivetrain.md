# Vehicle drivetrain

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P09 progress, following [`physics-vehicle.md`](physics-vehicle.md).

## Design

`src/physics/drivetrain.elisa` (`PhysicsDrivetrain`) is an integer
engine-and-gearbox model:

- `engine_rpm` follows the driven wheels through the gear and final-drive
  ratio, clamped to idle (900) and the limiter (6500), for either direction.
- `curve_nmm` is a three-point full-throttle torque curve (180/260/200 Nm).
- `wheel_torque_nmm` scales it by throttle and ratio, and gives no drive
  during a shift or at the limiter.
- `update` is a five-speed automatic that upshifts at 5800 rpm and
  downshifts at 2200 rpm with a 250 ms shift, so the rpm drop after an
  upshift lands inside the hysteresis band and never triggers a downshift.

## Checks

`test/physics_drivetrain.elisa` exits 0: rpm clamping and symmetry, curve
points, axle torque at full and half throttle, no drive at the limiter or
mid-shift, 100 frames just after an upshift with no hunting, and a full run
up to fifth and back to first in exactly 10 shifts. Negative control: moving
the downshift point to 3500 rpm makes the gearbox hunt and the test exit 9.

## Gaps

Not connected to `vehicle.elisa`'s tyres or to a Jolt vehicle; no clutch,
reverse or manual mode, so P09 stays open.
