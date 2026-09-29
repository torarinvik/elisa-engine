# 2D world step

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress. It joins [`physics-circles2d.md`](physics-circles2d.md)
and [`physics-sweep2d.md`](physics-sweep2d.md).

## Design

`src/physics/world2d.elisa` (`Physics2dWorld::step`) integrates up to 32
circles, builds their bounding boxes, finds candidate pairs with the
sort-and-sweep broadphase, resolves them with the circle narrowphase in the
broadphase's deterministic order, and then applies the ground. Each pair is
copied out and written back, because the narrowphase may not take two
mutable references into one array. The report gives candidates, resolved
contacts and broadphase overflow.

## Checks

`test/physics_world2d.elisa` exits 0: two balls rolling at each other
collide exactly once and swap velocities while staying on the ground, and a
dropped stack of three settles without interpenetration. Negative control:
not writing the second body back makes the test exit 2.

## Gaps

No sleeping, friction, joints, queries or 2D sample game, so P10 stays open.
