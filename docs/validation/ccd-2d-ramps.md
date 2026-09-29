# 2D CCD against slanted segments

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is P08 progress and follows
[`ccd-2d.md`](ccd-2d.md), which listed slanted surfaces as a gap.

## Design

`src/physics/ccd2d_ramp.elisa` (`Physics2dRamp`) sweeps circles against
one-sided segments. The front is the left of a → b, so a ramp authored left
to right is solid below.

- `distance_um` is the signed distance from the segment's line, using an
  integer normal and `isqrt` length.
- `sweep` reports a hit when the centre starts in front, ends within a
  radius and moves inward, at a contact point within the segment.
  - A circle already resting in contact and still pressing in is stopped at
    t = 0, so a slide under gravity does not sink step by step.
  - A circle behind the segment is ignored.
  - Placement uses a fraction in millionths. A 200 m/s drop stops within
    about 1 mm of contact. With permille placement, the first version left a
    gap of 1–10 mm.
- `bounce` reflects only the normal velocity, scaled by restitution, and
  keeps the tangential velocity, so an inelastic hit slides down the slope.
- `step` handles gravity and the earliest contact among up to four segments.

## Checks

`test/physics_ccd2d_ramp.elisa` exits 0, and it is wired into
`scripts/check.elisascript`. It covers:

- the exact distance;
- a 200 m/s drop that would tunnel without CCD;
- an inelastic hit leaving vx ≈ −vy (down the 45° slope);
- a 60-step slide under gravity that never sinks more than 5 mm;
- one-sidedness from below in both directions;
- a miss past the segment's end;
- an exact elastic bounce off a flat segment.

Negative control: removing the "behind the segment" rejection makes the
test exit 9. An earlier control, which only moved the ball upward from
behind, survived because the direction check already rejected it. That is
why case 9 was added.

## Gaps

- Segments aren't yet part of the 2D worlds' `step_all`.
- Contact at a segment's end caps, 3D CCD and solver budgets remain.
