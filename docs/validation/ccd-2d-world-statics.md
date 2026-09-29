# 2D worlds against walls, platforms and ramps

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is P08 progress and follows
[`ccd-2d-ramps.md`](ccd-2d-ramps.md), which listed world integration as a
gap.

## Design

- `Physics2dRamp::Statics` bundles a scene's static geometry: up to four
  walls, platforms and ramps each. Both 2D worlds (`Physics2dWorld::step`
  and `Physics2dSleepWorld::step`) now take `statics: Physics2dRamp::Statics&`
  instead of four wall and platform arguments.
- `step_statics` applies gravity, then calls `advance` up to three times.
  Each call moves through the remaining fraction of the step (in millionths)
  and stops at the earliest contact of any kind:
  - walls and platforms reflect their axis;
  - ramps reflect their normal.
- After a contact the rest of the step continues with the reflected
  velocity. This fixed a real defect found while wiring ramps in: a body
  resting on a ramp stopped at t = 0 every step and never slid. It also
  changes wall and platform behaviour, since a bounced body now rebounds
  for the rest of its step instead of stopping at the contact.
- `Physics2dRamp::step` (ramps only) now delegates to `step_statics`.

## Checks

- `test/physics_ccd2d_ramp.elisa` exits 0. Changed cases:
  - Case 5: a ball released at rest in contact slides more than 1.5 m in
    60 steps without sinking more than 5 mm.
  - Case 8: an elastic bounce ends at 56 mm, 6 mm of rebound after contact.
  - Case 10: in mixed geometry the nearer wall wins over the ramp, and the
    dart rebounds to x = 2.703 m.
  - Case 11: without the wall the same dart deflects off the ramp.
- `test/physics_world2d.elisa`, codes 10–12, drops a ball onto a ramp in the
  world. Over 60 steps it records at least 10 contacts, never gets within
  95 mm of a 100 mm radius, and ends past x = 1.5 m.
- `test/physics_world2d_sleep.elisa`, code 9: the ledge drop now checks the
  rebound over the rest of the step (y within 3.50–3.51 m, rising).
- `test/physics_ccd2d.elisa`, `physics_circles2d` and `physics_sleep2d` still
  exit 0.
- Negative control: limiting `step_statics` to one pass makes the world test
  exit 12 (the ball sticks) and the ramp test exit 5.
- While debugging, the first version of case 5 failed because a 141 m/s
  body slid off the end of the 4 m ramp. That was a flaw in the test, not
  in the engine, and the case now starts from rest.

## Gaps

- `Physics2dCcd::step_all` and `step` still use single-pass placement, but
  the worlds no longer call them.
- Segment end caps, 3D CCD and solver budgets remain.
