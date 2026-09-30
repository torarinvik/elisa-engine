# Pose scrub frame time (M02)

Measured on 2026-09-30 on SDL3/Metal (Apple silicon, 24 GB, load average
about 7–8 from other sessions). The stage1 compiler was run with
`ELISA_ALLOW_STALE_STAGE1=1`.

```bash
python3 scripts/pose_scrub_benchmark.py
```

The script cooks a 64-joint skinned rig (`build/cooked/subsets/scrub-rig.pkg`,
the self-test panel with its tip extended into a joint chain). It then builds
`test/render_scene_pose_scrub_main.elisa` through the render-scene smoke
harness in render-only mode, so no shared fixtures are recooked.

The main fills a 10,000-frame, 64-bone, 120 Hz `MocapClip` take and scrubs it
with random jumps (frame `step * 7919 mod 10000`), the worst case for a
timeline drag. Each frame does the following:

1. `PosePlayback::show_clip` samples the take;
2. `RenderScene::fill_pose_from_playback` fills the render pose;
3. the pose is submitted to the skinned instance;
4. the frame is presented with `Application::pump`;
5. the pose is completed.

The first 30 frames are warm-up; 600 frames are measured. The harness runs
the binary three times.

| run | frame median | frame p95 | frame max | scrub work median | scrub work p95 |
| --- | --- | --- | --- | --- | --- |
| 1 | 8.421 ms | 9.561 ms | 12.197 ms | 0.283 ms | 0.288 ms |
| 2 | 8.438 ms | 9.543 ms | 15.176 ms | 0.283 ms | 0.290 ms |
| 3 | 8.490 ms | 9.356 ms | 10.191 ms | 0.279 ms | 0.289 ms |

The whole-frame p95 stays under the 16.6 ms (60 Hz) budget that the script
enforces (exit 3 if exceeded). The median sits at the display's 120 Hz
present interval. The scrub work itself (sampling, filling and submitting 64
bones) is about 0.28 ms, so random-access scrubbing is present-bound, not
sample-bound. The C++ bridge in this harness is built at `-O0`.

## Limits found

- The glTF skin cooker caps a skin at 64 joints (`MAX_JOINTS` in
  `scripts/cook_gltf_skin.py`). The native submission accepts 256. M02's
  target of about 70 bones therefore cannot be cooked yet, so this
  measurement uses 64. Raising the cook limit is open, and M02 stays
  unchecked until then.
- The native bridge only accepts a submitted pose whose bone and morph counts
  equal the instance skeleton's, and each submission must be completed before
  the next.
