# Shadow cascade policy

Validated on 2026-09-29 with the Stage1 compiler freshly seeded that day (no
stale override needed). This is R05 progress.

## Design

- `src/backend/shadow_cascades.elisa` (`BackendShadowCascades`) computes sun
  cascade split distances with the practical split scheme: a per-mille blend
  of logarithmic and uniform splits, in integer millimetres. The last cascade
  ends at the far distance exactly; up to four cascades.
- Projections are stabilised: `stable_radius` rounds the bounding radius up to
  a fixed step, and `snap_um` floors the light-space origin to whole texels
  (`texel_um`), including negative coordinates, so camera motion does not
  shimmer shadow edges.
- `blend_permille` fades into the next cascade over a band before each split.

## Checks

- `test/backend_shadow_cascades.elisa` exits 0 (codes 1–14): exact uniform and
  logarithmic splits, monotonic practical splits, invalid input, radius
  rounding, texel size, snapping across zero, sub-texel stability, blend band.
- Negative control: truncating instead of flooring negative coordinates makes
  the test exit 11.

## Gaps

- `RenderScene.set_sun_cascade_distances` still takes hand-set distances; the
  native renderer does not consume the snapping yet, and there are no
  reference captures. R05 stays open. No proof harness.
