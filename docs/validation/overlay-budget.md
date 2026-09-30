# Overlay frame budget (M05)

```sh
python3 scripts/overlay_budget_benchmark.py
```

The script builds `test/render_scene_overlay_budget_main.elisa` through the
render-scene smoke harness (render-only mode) and runs it three times on a
hidden 640x400 SDL3/Metal scene. The environment is the one in
[`debug-drawing.md`](debug-drawing.md).

The take has 600 frames of a four-joint chain. The foot (joint 0) plants for 60
frames and swings for 60, stepping forward each cycle, and joint 3 is knocked
0.4 upward at frame 300. Every measured frame rebuilds the whole overlay from
the take:
- a heat-coloured speed trail over all 600 frames for each joint;
- three onion ghosts on each side of a frame that moves each step;
- a ground-contact cross at each touch-down found by `FootContact`.

That is 2,424 lines. The frame submits them through `RenderScene::debug_line_records`
in 256-line chunks and presents. It records 600 frames after 30 warm-up frames.
The last frame centres the ghosts on the knock and is saved to
`build/overlay-budget.png`.

The script fails if any run's frame p95 is above 16.6 ms. It also fails if the
capture has fewer than 4 heat-red pixels, or if they spread more than 80 pixels
in x or y. The spike must be flagged, and nothing else may be.

On macOS 27 (Retina capture, 1280x800):

| run | frame median | frame p95 | frame max | overlay work median | overlay work p95 |
| --- | --- | --- | --- | --- | --- |
| 1 | 8.05 ms | 9.31 ms | 10.87 ms | 3.17 ms | 3.23 ms |
| 2 | 8.07 ms | 9.31 ms | 9.52 ms | 3.18 ms | 3.25 ms |
| 3 | 8.13 ms | 9.33 ms | 9.68 ms | 3.19 ms | 3.25 ms |

The capture has 31 heat-red pixels, all on the knocked segment (x 679, y 316–347).
The swing arcs are green to yellow and the contact crosses are orange. The first
version of this run showed that the heat rule was wrong: red covered whole swing
arcs, because a planted foot's median speed was near zero. `OverlayTrail::heat`
now uses the median of the moving values (see [`overlay-trail.md`](overlay-trail.md)).
That change also cut the overlay work from about 8.8 ms to 3.2 ms, because the
rank-counting median runs over fewer values.

The overlay work is rebuilt every frame here, which is the worst case while
editing. The median's rank counting is O(n²) in the trail length, so a longer
trail window would need a sorting median.
