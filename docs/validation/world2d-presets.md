# 2D world solver presets

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is P08 progress. It follows
[`ccd-2d-end-caps.md`](ccd-2d-end-caps.md).

## Design

`src/physics/world2d_presets.elisa` (`Physics2dPresets`) adds three presets
for `Physics2dWorld::step`:

| Preset | Substep | Max substeps | Frame budget |
| --- | --- | --- | --- |
| `fast` | 16 ms | 1 | 16 ms |
| `balanced` | 8 ms | 4 | 32 ms |
| `stable` | 4 ms | 8 | 32 ms |

- `step_frame` runs a frame as whole substeps. A remainder shorter than one
  substep runs as a final, shorter step.
- Frame time past the budget is clamped. It is reported as `dropped_ms`
  rather than run as one tunnelling step or as unbounded catch-up.
- The per-step diagnostics (`resolved`, `wall_hits`, `overflowed`) are
  summed over the frame.
- `max_overlap_um` measures the deepest circle–circle or ground penetration.

## Measurements

`test/physics_world2d_presets.elisa` drops a five-ball tower (radius 0.1 m,
restitution 0.2) and runs 188 frames of 16 ms. It records the worst overlap
over the last second:

| Preset | Worst overlap | Top ball height | Budget asserted |
| --- | --- | --- | --- |
| `fast` | 118 mm | 0.62 m | ≤ 130 mm |
| `balanced` | 64 mm | — | ≤ 70 mm |
| `stable` | 17 mm | 0.86 m (ideal 0.9 m) | ≤ 20 mm, top ≥ 0.84 m |

The test also checks:

- the substep counts (188, 376 and 752);
- a strict ordering stable < balanced < fast;
- that a 100 ms hitch under `stable` runs 8 substeps and drops 68 ms;
- that a 10 ms `balanced` frame runs 8 + 2 ms.

## Checks

- The test exits 0 and is wired into `scripts/check.elisascript`.
- Negative control: running the whole remaining frame as one step exits 1.
- The source-length check passes.

## Gaps

- The presets drive only the plain 2D world. The walkable-surface world and
  3D have no presets.
- The budgets are measured on one stack scene, not on a scene suite, and
  there is no per-preset CPU timing.
- A fast-preset stack still overlaps by more than half a radius. A
  position-correction pass is the fix, but it is not done.
