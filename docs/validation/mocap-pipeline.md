# Headless mocap cleanup pipeline (M07, pipeline part)

Date: 2026-09-30. Branch: `mocap-track`.

## What is covered

`MocapPipeline` (`src/assets/mocap_pipeline.elisa`) cleans animation tracks with no window, GPU or application loop.

- **`clean_animation(doc, animation, settings)`** runs `MotionFilters::despike` (a running median, then a Gaussian) on each selected translation and rotation output track.
  - Rotations first go through `quat_continuity` and are renormalised after filtering.
  - An output shared by several channels is filtered only once.
  - These channels are skipped and counted in the report: CUBICSPLINE, weights, scale, anything longer than `MotionFilters::capacity()` (2048 keys), and anything shorter than 3 keys.
- **`clean_all`** cleans every animation. **`clean_file(input, output, settings)`** runs load → clean → save.
  - A load failure raises `GlbDocumentError` before the output is opened, so no file is written.

## Test

`test/assets_mocap_pipeline.elisa` (registered in `scripts/check.elisascript`):

```
ELISA_ALLOW_STALE_STAGE1=1 elisac-stage1 -emit exe -o build/assets_mocap_pipeline-test test/assets_mocap_pipeline.elisa
./build/assets_mocap_pipeline-test   # exit 0 = pass
```

- **Missing input:** `clean_file` reports `OpenFailed`, and no output file is created.
- **Real take** (`../elisa-boxing-game/build/dual-stance/black/black-boxer.glb`, read only; skipped if absent):
  - A 50-unit spike injected into the `jab` Hips translation is removed, ending within 5 units of the original value.
  - Only bytes from the BIN chunk onward change.
  - Every RightForeArm rotation key stays unit length.
  - Running `clean_file` over the whole file produces a same-length GLB that reloads with the same animation count.
  - Two runs give byte-identical output, which `cmp` also confirms.
- **Speed:** the whole test, including three loads of 55.9 MB and two full clean-and-save runs, takes about 9 s wall time on an Apple M5.

## Batch CLI and thumbnails (2026-10-01)

- **CLI.** `build/mocap-clean` (tools/mocap_clean.elisa, built by `scripts/build_mocap_clean.sh`) cleans every `*.glb` in `MOCAP_IN` into `MOCAP_OUT` with the default settings. It also writes `NAME.png`, the pose at key `MOCAP_THUMB_KEY` (default 0) of the first animation. It needs no window or display. `native/mocap_folder.c` lists the folder in sorted order and joins paths, because Elisa has no directory API. A bad file prints `FAIL`, the run goes on, and the exit code counts the failures (101 means a missing folder).
- **Thumbnail.** `MocapThumbnail` (src/viewport/mocap_thumbnail.elisa) samples the node TRS channels at the key; CubicSpline values are read at `3k+1`. It composes world positions up the parents, frames them with `ViewportCamera::frame_points` and draws the grid, bones and joints into an offscreen `Viewport`. It reads the frame back and writes it with `PngWriter`. `MocapThumbnailPolicy` holds the key-to-value index rule: past-the-end keys hold the last key, and CubicSpline reads skip the in-tangent. It is proved in proof/mocap_thumbnail_policy.elisa (36/36, all replayed).
- **Smoke** (`python3 scripts/mocap_batch_smoke.py`; skips when the boxer file is absent). The folder holds the boxer GLB, a malformed `broken.glb` and a `.txt` file.
  - `broken.glb` is reported and the boxer still finishes (577 channels filtered).
  - The exit code is 1.
  - Two runs give byte-identical GLB and PNG output: the stated PNG tolerance is 0 differing bytes on the reference machine (Apple M5).
  - The thumbnail has 608 bone-coloured pixels.
  - Key 40 differs from key 0 on 8,622 pixels.
  - A mutant that ignores the animation channels fails with exit 15.
  - A run takes about 1.2 s.

## Remaining

- Filter settings are global per run. Per-bone or per-channel settings, and the IK and grounding passes from M08, are not wired in yet.
- The thumbnail draws the skeleton, not the skinned mesh; that waits on the M01 skinned-mesh work.
