# 2D circle dynamics

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress.

## Design

`src/physics/circles2d.elisa` (`Physics2dCircles`) is integer 2D circle
dynamics with its own units: micrometres, micrometres per second and grams.
A 2D game therefore never goes through Jolt or a 3D transform.

- `integrate` applies gravity with semi-implicit Euler. A static body
  (mass 0) is left alone and reports false.
- `ground` pushes a circle out of a floor, reflects its falling speed by the
  restitution, and stops bounces slower than a rest speed so bodies settle.
- `pair` resolves circle-circle overlap: a positional correction split by
  inverse mass, then a restitution impulse along the contact normal when the
  bodies are closing. Separating contacts only correct positions, coincident
  centres separate along +x, and two static bodies are ignored.
- `isqrt` is an integer Newton square root.

## Checks

`test/physics_circles2d.elisa` exits 0. It covers square roots, a dropped
ball bouncing at least three times and resting exactly on the ground, an
equal-mass elastic swap that conserves momentum exactly, separating and
distant pairs, a static wall reflecting at half restitution without moving,
and coincident centres. Negative control: dropping restitution from the
impulse makes the test exit 5 (the swap fails).

## Gaps

No broadphase, polygons, joints, queries or friction, and no Box2D binding
or 2D sample game, so P10 stays open.
