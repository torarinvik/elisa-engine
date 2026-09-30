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

## Points

`points(xyz, rgba, size)` draws each point as a three-axis star of half-size
`size` in one colour: three records per point, so points share the line batch
and its all-or-nothing submission. It draws nothing for a ragged point list, a
colour that is not four values, a size that is not positive and finite, or more
than 65,536 points. `OverlayLineIndex::point_lines` is the shape and budget
check. Its bounds are proved: the result is at least 0, at most 196,608, and
either 0 or the value count. The exact ragged and whole cases are tested only,
because the prover times out on `%`. The test checks star coordinates and
alpha, and every rejection. The mutation check caught every mutant: the size,
ragged, colour, alpha and budget-edge mutants, some through the postcondition.

## Labels

Labels have their own native batch, beside the lines. For each label,
`RenderScene::debug_label_records` takes a position and colour, the UTF-8 bytes
and a byte count. It submits 64 labels per native call, and the batch holds
1,024 labels between flushes. A label must be 1 to 63 bytes with no zero byte.
Batched labels face the camera and are a fifth of a world unit tall. The
Elisa side and the native side each check every chunk whole, and a chunk that
fails leaves nothing queued. Wicked rasterises glyphs lazily, so text drawn for
the first time comes out blank; the smoke test draws one warm-up frame first.
See [`debug-drawing.md`](debug-drawing.md) for the test.

## Still open

Nothing for M05. Labels have no batched-label mode for a fixed pixel height yet.
