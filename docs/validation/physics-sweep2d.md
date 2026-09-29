# 2D sort-and-sweep broadphase

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress, following [`physics-circles2d.md`](physics-circles2d.md).

## Design

`src/physics/sweep2d.elisa` (`Physics2dSweep`) finds overlapping pairs among
up to 32 2D bounding boxes. Bodies are sorted by (min x, index) with a
stable insertion sort, then swept along x with a y check. Pairs are stored
lower index first in sweep order, so the narrowphase sees the same pairs in
the same order on every run. A full 64-pair buffer sets `overflowed` instead
of dropping pairs silently.

## Checks

`test/physics_sweep2d.elisa` exits 0: a separated/overlapping/y-disjoint set
yields exactly the one real pair; a dense row of 12 boxes yields 30 pairs,
matching a brute-force count; a repeated sweep yields the same first pair;
and 32 stacked boxes fill the buffer and report overflow. Negative control:
limiting the sweep to adjacent neighbours makes the test exit 3.

## Gaps

No incremental update between frames, no layers or masks, and no link to
`circles2d` stepping or a 2D sample game, so P10 stays open.
