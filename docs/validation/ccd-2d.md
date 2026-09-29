# 2D continuous collision

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is P08 progress on thin-wall projectiles and fast movers,
for the 2D path in `src/physics/circles2d.elisa`.

## Design

`src/physics/ccd2d.elisa` (`Physics2dCcd`) sweeps a circle's leading edge
against thin vertical walls within one step.

- `sweep` returns whether contact happens and when (in permille), plus the
  exact horizontal travel to contact. Placement uses that travel, so the
  rounding of the permille time never leaves a gap or an overlap.
- Contacts where the circle passes above or below the wall are ignored, as
  are circles moving away or already past it.
- `step` applies gravity, stops at the earliest contact among up to four
  walls, and reflects the horizontal speed with restitution.

## Checks

- `test/physics_ccd2d.elisa` exits 0. It covers:
  - a 400 m/s, 5 mm bullet at a 16 ms step against a 1 mm wall. Plain
    integration tunnels through it; CCD stops it at the face and bounces it
    at half speed;
  - no false hits while it moves away;
  - passing over the wall top;
  - hitting the far face from the right;
  - the nearest of two walls winning;
  - a 50-step run under gravity being identical twice.
- Negative controls:
  - Taking any hit instead of the earliest makes it exit 9.
  - Without exact-travel placement, the old permille-only placement
    failed code 3; that is why the fix exists.
- Wired into `scripts/check.elisascript`.

## World integration

`Physics2dSleepWorld::step` now moves awake bodies with `Physics2dCcd::step`
against up to four walls, and `Report.wall_hits` counts contacts. Code 8 of
`test/physics_world2d_sleep.elisa` fires a 300 m/s ball at a 1 mm wall
inside the world. Negative control: passing zero walls to the sweep makes
it exit 8. The earlier sleeping and friction cases still pass. Built with
`ELISA_ALLOW_STALE_STAGE1=1`, because another session had newer compiler
sources than the binary.

## Gaps

- Only axis-aligned vertical walls; there are no general segments or
  circle-circle CCD.
- `world2d` (the non-sleeping world) does not call it yet.
- Jolt CCD for 3D, solver budgets and measured presets remain.
