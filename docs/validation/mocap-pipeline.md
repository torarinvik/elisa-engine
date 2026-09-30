# Headless mocap cleanup pipeline (M07, pipeline part)

Date: 2026-09-30. Branch: `mocap-track`.

## What is covered

`MocapPipeline` (`src/assets/mocap_pipeline.elisa`) cleans animation tracks with no window, GPU or application loop.

- **`clean_animation(doc, animation, settings)`** runs `MotionFilters::despike` (a running median, then a Gaussian) on each selected translation and rotation output track.
  - Rotations first go through `quat_continuity` and are renormalised after filtering.
  - An output shared by several channels is filtered only once.
  - These channels are skipped and counted in the report: CUBICSPLINE, weights, scale, anything longer than `MotionFilters::capacity()` (2048 keys), and anything shorter than 3 keys.
- **`clean_all`** cleans every animation. **`clean_file(input, output, settings)`** runs load → clean → save.
  - A load failure raises `GlbError` before the output is opened, so no file is written.

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

## Remaining

- **Offscreen PNG render of a frame.** The only existing capture is `ApplicationCapture`'s screenshot request, which needs a running windowed application. The engine has no offscreen render-target readback or PNG encoder that a headless process could call, so this needs engine work in the renderer bridge.
- **Folder-level CLI.** `clean_file` is the unit a batch tool would loop over. A command-line entry point with directory listing and argument parsing is not written.
- Filter settings are global per run. Per-bone or per-channel settings, and the IK and grounding passes from M08, are not wired in yet.
