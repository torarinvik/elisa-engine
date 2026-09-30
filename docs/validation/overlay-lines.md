# Overlay line records

`OverlayLines` (src/animation/overlay_lines.elisa) turns M05's overlays into line records of ten values each: start xyz, end xyz and rgba. A renderer can submit them in batches.

- `trail(points, heat)` gives one record per segment, coloured by `OverlayTrail::colour` of that segment's heat. A missing heat value draws cold.
- `ghost(positions, frames, parents, frame, alpha)` draws the skeleton's bones at one frame in grey. Roots, out-of-range parents and self-links draw nothing.
- `ghosts(..., center, step, count)` draws up to `count` ghosts each way, `step` frames apart, where the clip has them. The k-th pair has alpha 0.6 × (count + 1 − k) / (count + 1).
- `contacts(markers, up, size)` draws an orange cross on the ground plane at each marker, for y-, z- or x-up.

`OverlayLineIndex` (src/animation/overlay_line_index.elisa) holds the record slots, the bone-link rule and `frame_lines`, the number of lines a frame of overlays needs. Its proof (proof/overlay_line_index.elisa) is 55/55 with every obligation replayed. An upper bound on the slot was dropped: `line * 10 + k < lines * 10` times out as a nonlinear goal.

## Validation

test/animation_overlay_lines.elisa runs in the check gate on a 600-frame four-joint chain with a knock at frame 300.

- Full-length trails for all four joints give 4 × 599 records. The knocked segment is red and the one before it is not.
- Three ghosts each way at step 5 give 18 bone records, with the expected positions and alphas (0.45 nearest, 0.15 farthest).
- Trails, ghosts and two contact crosses total 2,418 lines, which matches `frame_lines`.
- The test also covers:
  - ghosts clipped at both clip edges;
  - odd parent links;
  - bad frames, steps and counts;
  - cross geometry for all three up axes;
  - missing heat.

The mutation check caught every non-equivalent mutant. One survivor is equivalent: `step < 0` for `step < 1`, because a zero distance never has a ghost.

## Still open

The native debug bridge takes 128 commands per flush, and the budget case needs 2,418 lines. Next come a batched native line submission, the frame-budget run and the captured PNG.
