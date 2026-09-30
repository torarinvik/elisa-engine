# Pelvis smoothing

`src/animation/pelvis_smoothing.elisa` ports the pelvis step of the boxing
game's `tools/clean_leg_motion.py`:

- `smooth_axis` moves each path sample toward its Gaussian-smoothed value
  (sigma 3 frames), weighted by the clip-end fade. A pelvis that drops into a
  landing and stops dead no longer snaps the knees.
- `drop` low-passes pelvis height (sigma 8 frames) and keeps only the part
  below the source, smoothed again (sigma 3) and faded. The result is never
  positive: raising the pelvis would straighten a landing leg into a bounce.

Looping clips use full weight everywhere. Signals are caller-owned
`MotionFilters::Signal` buffers, with scratch passed in, so nothing large is
built by value.

## Proofs

`proof/pelvis_smoothing_index.elisa` proves `PelvisSmoothingIndex::fade_span`:

- a looping clip has a span of 0;
- a clip keeps a positive edge unchanged;
- the span always stays within `0..edge`.

That gives 20/20 goals and replay with 0 gaps. The per-frame fade itself
comes from the proved `FootPivotIndex::edge_steps`.

## Tests

- `test/animation_pelvis_smoothing.elisa` checks properties:
  - the drop is never positive;
  - the drop is zero at the clip ends and negative on a bobbing middle;
  - a steady pelvis is left alone;
  - the end samples are kept.
- `test/animation_pelvis_smoothing_reference.elisa` checks the edge and wrap
  modes against `tools/pelvis_smoothing_reference.py` (numpy, the tool's own
  `smooth()`) to 1e-9.
- Mutation check: removing the lower-only clamps fails both tests (exits 3
  and 13).
