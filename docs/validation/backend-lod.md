# Screen-error LOD choice

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R10 progress. It is the selection policy only; it is not connected to
the Wicked LOD bridge, cooked LOD chains or the large-scene cost measurement.

## Design

`src/backend/lod.elisa` (`BackendLod`) takes up to four levels with a
non-decreasing geometric error in mm (level 0 exact) and a `View` (focal
length in pixels, pixel-error threshold, draw distance, hysteresis in
permille, at most 500). Projected error is `error * focal / distance` in
integer pixels, and `choose` picks the coarsest level that fits. Integer
truncation means the level-1 boundary in the test is about 6.67 m.

- Beyond the draw distance, at a negative distance, or with invalid levels or
  view, `choose` returns hidden (-1) rather than guessing.
- Hysteresis: last frame's level is kept until the new one would still fit a
  margin nearer (going coarser) or the current one no longer fits a margin
  farther (going finer). An out-of-range previous level is ignored.
- A level count below 4 never selects a missing level.

## Checks

- `test/backend_lod.elisa` exits 0 (codes 1–14).
- Negative control: removing the hysteresis margin makes it exit 7.
- Source-length check passes. The prover was not run on this module.

## Gaps

- Not wired to render scenes, no visual quality comparison and no large-scene
  frame-cost measurement, so the done condition of R10 is not met.
- Full gate still blocked at `world-test`.
