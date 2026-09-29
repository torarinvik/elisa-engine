# 2D CCD end caps

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is P08 progress and follows
[`ccd-2d-world-statics.md`](ccd-2d-world-statics.md), which listed segment
end caps as a gap.

## Design

- `Physics2dRamp::sweep` now returns the earliest of the one-sided face
  (`sweep_face`, unchanged) and both rounded ends (`sweep_cap`).
- `sweep_cap` solves the swept-disc-against-point quadratic in millimetres,
  the same scaling as `Physics2dCcd::sweep_pair`, so 10 m scenes stay inside
  i64.
  - Caps are solid from every side.
  - An overlapping circle that is still moving in stops at once.
- `Contact` now carries its normal: the segment normal for the face, or the
  centre-at-contact minus the endpoint for a cap. `bounce` reflects along
  that normal, so a tip hit deflects around the corner instead of off the
  ramp's plane.

## Checks

`test/physics_ccd2d_ramp.elisa` exits 0. New cases:

- Codes 12–13: a ball flying left into the lower tip misses the face, hits
  the cap at x = 4.05 m, and rebounds elastically to x ≈ 4.70 m with vy
  unchanged.
- Code 14: 60 mm below the tip (radius 50 mm) the ball passes by untouched.
- Code 15: a 30 mm offset glances off inelastically, deflected down and
  still moving left.

The 2D world and sleep-world tests still exit 0.

Negative control: returning only the face contact makes the test exit 12.

## Gaps

- Cap resolution is 1 mm.
- 3D CCD, solver budgets and presets remain for P08.
