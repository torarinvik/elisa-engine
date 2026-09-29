# 2D ray and point queries

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the Stage1 compiler.
This is P10 progress, alongside [`physics-world2d.md`](physics-world2d.md).

## Design

`src/physics/query2d.elisa` (`Physics2dQuery`) queries 2D circles in
micrometres. `ray_circle` gives the entry point as a per-million fraction of
the segment, 0 when the ray starts inside, and -1 for a miss (pointing away,
too short, or passing beside). The discriminant is computed in millimetres
so products stay in i64. `cast` returns the nearest hit, ties to the lower
index, and `contains` is a point query.

## Checks

`test/physics_query2d.elisa` exits 0: a hit at 0.45 of the ray, misses
pointing away, too short and offset, a start inside, the nearest of three
bodies at 0.3, a full miss, and point containment on either side of the
edge. Negative control: taking the last hit instead of the nearest makes the
test exit 6.

## Gaps

Circles only; the millimetre discriminant loses sub-millimetre precision;
no shape casts or layer masks, so P10 stays open.
