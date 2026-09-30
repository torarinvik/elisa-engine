# Viewport motion overlays (plan M05)

## API

`MotionOverlay` (src/viewport/motion_overlay.elisa). Every call appends to a
`ViewportDraw::List`, so the whole overlay is one batched upload that the
Metal viewport and the CPU reference rasterizer both draw. Tracks are
`darray[Geometry::Vec3]` indexed by frame. Calls return how many items they
drew, or a `ViewportDraw::DrawError` (non-finite point, capacity, bad layer).

- `points(list, cam, pts, radius_px, color, layer)`: screen-sized markers.
- `trail(list, track, frame, before, after, color, layer)`: a polyline over
  the window `[frame - before, frame + after]`.
- `heat_trail(list, track, frame, before, after, heat, layer)`: the same
  window coloured per segment. `Heat{mode: SPEED | ACCELERATION, low, high,
  fps}` maps values onto the 8-colour `ramp_color` (blue to red). A NaN value
  is shown hottest so that bad data stands out. An empty or inverted range
  saturates. `segment_value` and `bucket_of` are public, so a host can list
  the flagged frames.
- `contacts(list, cam, track, frame, before, after, fps, ground_y,
  height_limit, speed_limit, radius_px, color, layer)`: markers dropped to the
  ground wherever the joint is low enough and slow enough.
- `ghost(list, rig, models, color, layer)`: an onion-skin skeleton (bones
  only) from the model transforms evaluated at another frame.
- `ghost_frame(frame, k, step, count)`: the frame of ghost `k`; negative `k`
  is earlier.
- `ghost_color(k, ghosts)`: earlier ghosts are cool, later ghosts are warm,
  and both fade with distance.

Window semantics: the frame is clamped into the clip first, and the window is
applied after that. The window spans are capped at 1,000,000 and negative
spans count as 0.

## Proof

`MotionOverlayPolicy` (src/viewport/motion_overlay_policy.elisa) holds all
the index arithmetic. The proof shows that:

- window bounds and ghost frames are always inside `[0, count - 1]`;
- heat buckets are inside `[0, 63]`, and a non-positive value is always
  bucket 0;
- a contact implies both limits hold.

```
/private/tmp/claude-501/mc/elisa-proof "$PWD/proof/motion_overlay_policy.elisa"
# certificate replay: 72 replayed, 0 gaps
```

## Test

```
ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN="$HOME/.elisac/stage1/scripts/elisac_stage1.sh" \
  bash scripts/build_viewport_native_test.sh viewport_motion && ./build/viewport_motion-test
```

The test is also run by `scripts/check.elisascript`. Results on an Apple M5,
2026-09-30, rc 0:

- **Spike.** A 600-frame sweep at 0.2 m/s has frame 300 lifted by 5 cm. It
  is drawn as a speed heat trail in a front orthographic view, rasterised on
  the CPU, and written to `build/viewport_heat_spike.png`.
  - 6 red pixels within 12 px of frame 300's column.
  - 0 red pixels elsewhere.
  - 189 cool (blue) pixels along the rest of the trail.
  - In acceleration mode, the spike is in the hottest bucket and a steady
    frame is in bucket 0.
- **Windows.** Interior, start and end windows give exact segment counts
  (30, 25, 14). A frame far out of range is clamped. Negative spans draw
  nothing. Spans near i64 max are capped to the whole clip.
- **Ghost frames.** Stepping, clamping at both ends, and oversized `k` and
  step all behave as specified. Ghost colours differ by direction and fade.
- **Contacts.**
  - A foot planted for 100 frames gives 99 markers; the takeoff frame is
    excluded because its forward segment lifts.
  - A lifted foot gives 0.
  - A foot sliding at 1.2 m/s on the ground gives 0.
- **Adversarial.**
  - Empty and one-frame tracks draw nothing.
  - A NaN point makes `trail` return a `DrawError`.
  - NaN, below-range, far above-range and inverted-range heat values map as
    specified.
  - Ramp lookups outside the ramp are clamped.
  - Out-of-range segment indices give 0.
- **Budget.** Each round builds 4 heat trails × 600 frames, contacts on two
  feet and 6 ghost skeletons (3 on each side), then runs `Viewport::tick` at
  1280x720 on the synchronous Metal path. Worst of 4 timed rounds: 1.18 ms,
  against a 16.6 ms budget. Without a Metal device, this part is skipped with
  a message.

## Open

- Labels: the viewport renders lines and triangles only. Joint names and
  frame numbers should be drawn by the hosting elisa-ui panel, using
  `ViewportCamera::project` for placement.
- Ghosts over the skinned mesh, which is not in the ring yet (see M01).
