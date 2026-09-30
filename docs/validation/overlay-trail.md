# Overlay trails and heat colouring

`OverlayTrail` (src/animation/overlay_trail.elisa) builds the data behind M05's motion trails. It does not draw.

- `trail(positions, frames, joints, joint, center, before, after)` copies one joint's xyz positions out of a frame-major clip over `[center - before, center + after]`. The window is clamped to the clip. A malformed shape or joint gives an empty trail.
- `speeds` returns one value per segment: distance × fps. `accelerations` returns one value per interior sample: the second difference × fps².
- `median` is the lower median, found by rank counting without allocating.
- `heat(values, factor)` is each value over `factor × median`, clamped to [0, 1]. With a zero median, any motion is fully hot and stillness is cold. `spikes` lists the indices above that limit.
- `colour(h)` ramps blue → green (1/3) → yellow (2/3) → red (1). Out-of-range values clamp and NaN is treated as 0.

`OverlayIndex` (src/animation/overlay_index.elisa) holds the integer rules: window ends, position slots and whether an onion ghost `distance` frames before or after a centre frame exists. Its proof (proof/overlay_index.elisa) is 97/97 with every obligation replayed.

## Validation

test/animation_overlay_trail.elisa runs in the check gate:

- The clip is 600 frames of a joint circling every 120 frames, with a 0.5 knock at frame 300.
- Speeds and accelerations match a Python reference to 1e-9 and 1e-7.
- Segments 4 and 5 are flagged and fully hot. Steady motion sits at 1/3. A still root is cold.
- The test also covers clamped windows, bad shapes, one- and two-point trails, a full 600-frame trail, median ordering, the colour stops and clamping, and the ghost existence rules.

The mutation check caught every non-equivalent mutant. Two survivors are equivalent:

- `t <= 2/3` in `colour`: both branches give [1, 1, 0] at 2/3.
- `n > 1` for the acceleration stop: at n = 2, n − 1 equals the fallback.

## Still open

- Drawing through `RenderScene::debug_line`.
- Ground-contact markers.
- The 4 joints × 600 frames + 3 ghosts frame-budget run.
- The captured PNG of the flagged spike.
