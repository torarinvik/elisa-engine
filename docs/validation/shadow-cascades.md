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

## Renderer wiring (2026-10-01)

- `RenderScene::set_sun_cascade_policy(near_mm, far_mm, lambda_permille)`
  computes Wicked's three sun cascade end distances with `split_end` and
  applies them through `set_sun_cascade_distances`, which still checks them
  against the active camera's clip range. Invalid policies (lambda past 1000,
  far not past near) raise `InvalidValue` before any native call.
- Texel snapping of each cascade projection is done by Wicked itself
  (`CreateDirLightShadowCams`, "Snap cascade to texel grid"), so the engine
  hands it split distances only. `snap_um`/`stable_radius` stay the
  backend-neutral policy for adapters without their own snapping.
- The integer helpers now live in float-free `BackendShadowCascadeGrid`
  (`src/backend/shadow_cascade_grid.elisa`); `BackendShadowCascades` forwards
  to them. `blend_permille` clamps its scaled weight in place instead of
  calling `ramp`, which was removed.

## Reference captures

`test/render_scene_cascade_native.elisa` (render group 197, cases 240–253)
runs inside the isolated lit caster/receiver fixture. A uniform 300 m policy
(`100, 300000, 0`, splits 100.067/200.033/300 m) puts the receiver in a 100 m
cascade and its cast shadow is visibly blurred; a practical 12 m policy
(`100, 12000, 500`, splits 2.28/5.233/12 m) gives a sharp shadow. The test
checks both sets of live Wicked distances, requires a 0.01 patch-luminance
change, then restores 10/100/400. The smoke saves
`render-scene-shadow-cascade-{wide,tight}.png`, and
`docs/validation/references/lighting/cascade-{wide,tight}.png` join the
lighting references (now fourteen). A rerun matched both at peak/mean 0/0.

## Proof

`proof/shadow_cascades.elisa` proves `blend_permille` stays within 0..1000 for
every input and that a valid cascade's `texel_um` is at least 1 (41/41
obligations). Saturation at the split, snapping and radius rounding need
division facts or long guard disjunctions the prover does not handle; the
unit test covers them.

## Negative controls

- Policy ignoring `lambda_permille` (always uniform): smoke exits 197,
  case 248.
- Dropping the `size < 1` clamp in `texel_um`, or returning 1001 from the
  blend clamp: the proof fails (exit 1).

## Validation

Full gate (`elisascript scripts/check.elisascript`) ended with "Validation report written."; SDL3/Metal render-only smoke (`ELISA_RENDER_SCENE_RENDER_ONLY=1`) exit 0
with all fourteen lighting references passing; `test/backend_shadow_cascades`
exit 0; stale stage1 override (`ELISA_ALLOW_STALE_STAGE1=1`) in use.
