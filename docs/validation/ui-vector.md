# Integer vector shapes

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I08 progress.

## Design

`src/ui/vector.elisa` (`UiVector`) is the geometry a vector renderer needs,
in whole device units, with coordinates bounded to +-1,000,000.

- `steps_for` chooses how many segments flatten a quadratic curve within a
  pixel tolerance. The error grows with the control point's deviation, so
  steps grow with its square root (integer `isqrt`), from 1 for a straight
  curve to 32; a zero tolerance or out-of-range point gives 1.
- `point_at` evaluates the quadratic exactly at k/n in integers, rounded
  toward zero, and returns the end points for k <= 0 and k >= n.
- `winding` and `filled` apply the nonzero rule to a polygon of up to eight
  vertices, so reversed and doubled loops fill consistently and shapes with
  fewer than three vertices fill nothing.

## Checks

- `test/ui_vector.elisa` exits 0 (codes 1-13): square roots, step counts,
  finer tolerance needing more steps, exact points, fill inside and outside,
  reversed and doubled winding, degenerate and out-of-range input.
- Negative control: dropping the downward-edge case makes the reversed-shape
  check fail.
- Wired into `scripts/check.elisascript`.

## Gaps

- Quadratics and polygons only: no cubics, arcs, strokes, joins, caps,
  dashes, gradients or anti-aliased rasterisation, and nothing draws yet.
  I08 stays open.
- Polygons are capped at eight vertices.
- No proof written for this module.
