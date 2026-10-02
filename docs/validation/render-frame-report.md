# Render frame report and budgets

Validated on 2026-10-02 on macOS 27.0 / Apple M5 (Metal) with the pinned
Stage1 compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the pinned Wicked checkout.

## Design

`src/backend/frame_report.elisa` (`FrameReport`) accumulates per-pass CPU and
GPU time, draw counts, upload bytes and resource bytes into one `Report`, and
names the slowest pass. Its scalar GPU-label rules live in
`src/backend/frame_report_gpu.elisa` (`FrameReportGpu`).

- A GPU time of `Gpu::NONE` (`FrameReportGpu::UNAVAILABLE`, -1) means the
  driver gave no timing. One such pass makes the frame's GPU total
  unavailable (`gpu_measured` is false) and adds nothing to it; the other
  passes' CPU, draw and byte totals still sum. Callers print "unavailable",
  never 0.
- `record` refuses negative counts or a GPU time below -1 and returns the
  report unchanged, so a bad probe cannot lower a total. GPU totals saturate
  instead of overflowing.
- `judge` checks a frame against a `Budget` in the fixed order cpu, gpu,
  draws, uploads, resources; an unavailable GPU total is never a failure.
  `judge_frame` uses the cold budget for frame 0 and the warm budget after it.

## Native probe

`native/render_frame_report_probe.h` is compiled into render-smoke builds only
(`ELISA_RENDER_SCENE_TEST_PROBE`). `native/render_scene_path.inc` measures three
passes inside `ElisaRenderPath3D`:

| Pass | What is timed | GPU time | Draws | Bytes |
| --- | --- | --- | --- | --- |
| 0 update | `PreUpdate` + `Update` CPU | 0 by construction (no command list) | 0 | - |
| 1 scene | `Render()` CPU | timestamps around Wicked's lists | visible objects | upload delta |
| 2 compose | `Compose(cmd)` CPU | timestamps on `cmd` | composed views | upload delta, resident total |

GPU times come from a timestamp query heap resolved into a readback buffer and
`GetTimestampFrequency()` (the R17 technique). A pass gets -1 when the device
has no heap or frequency, a stamp was never written, the end precedes the
begin (Apple TBDR can do this), or the span exceeds one second. Uploads are
the positive change in `GetMemoryUsage().usage` inside a pass; resident bytes
are that total after compose.

`elisa_render_scene_v1_test_frame_report_capture(frames, env_name)` pumps the
frames, waits for the GPU, and writes JSON to the path in `env_name`; GPU
times the device did not give are written as the string `"unavailable"`.

## Tracy

Tracy v0.14.1 (`dependencies/tracy`) is now linked into the render smoke with
`TRACY_ENABLE` (`scripts/render_frame_report.py tracy_args`). Each pass opens a
named zone (`elisa_update`, `elisa_scene`, `elisa_compose`); each captured
frame plots the scene pass CPU time and draws with `TracyPlot` and ends with
`FrameMark`. The test
asserts the probe counted one `FrameMark` per captured frame. No Tracy server
is attached in the gate, so the zones are emitted but not captured to a
`.tracy` file there; attaching the Tracy profiler to a smoke run shows them.
Without a fetched Tracy the macros compile away and the count reads -1.

## Representative scene and thresholds

`test/render_frame_report_native.elisa` (render smoke group 240) uses the R15
authored multi-pass render graph scene. It resizes to 480x300 and rebuilds the
graph, so frame 0 is cold for that size: new render targets, new transients
and a new graph. Process-cold shader compilation is not in this frame; R13's
packaged shader smoke measures that separately. Frames 1-7 are warm.

For each of 8 frames it checks three passes, label consistency (a frame is
measured only if no pass is -1), scene draws > 0, compose resident bytes > 0,
and `judge_frame` against the committed budgets:

Two runs of `scripts/render_scene_native_smoke.py` on 2026-10-02 (Metal,
timestamps available, Tracy linked, 8 frames each). Run 1 was a scratch
worktree under the earlier loose budgets; run 2 was a sibling worktree of
commit 93aa5bc3 under the committed budgets, and the whole smoke passed.

| Figure | Run 1 | Run 2 | Cold budget | Warm budget |
| --- | --- | --- | --- | --- |
| cold CPU us (update/scene/compose) | 3017 (2496/497/24) | 2503 (1958/524/21) | 100000 | - |
| cold GPU us (scene/compose) | 2413 (2272/141) | 2414 (2280/134) | 50000 | - |
| cold draws | 4 | 4 | 8 | - |
| cold upload bytes | 6684672 | 6684672 | 67108864 | - |
| cold resident bytes | 580681728 | 580681728 | 805306368 | - |
| warm CPU us median / max | 633 / 1025 | 632 / 1134 | - | 50000 |
| warm GPU us median / max | 2404 / 2411 | 2401 / 2449 | - | 33000 |
| warm draws | 4 | 4 | - | 8 |
| warm upload bytes median / max | 0 / 524288 | 0 / 524288 | - | 4194304 |
| warm resident bytes max | 581156864 | 581156864 | - | 805306368 |

Counts and bytes reproduce exactly; times agree within about 20% for CPU and
2% for GPU. The cold frame's extra cost is the update pass reallocating
targets (4.1 MB) and the scene pass creating transients (2.6 MB). CPU and GPU
budgets leave wide headroom because the smoke runs on a loaded developer
machine; the draw, upload and resident budgets are the tight regression
checks (a doubled draw count, an 8x warm upload or about 220 MB more resident
memory fails). Resident bytes are the device total, so other smoke groups'
leftovers count too. On this machine every pass's GPU time was measured; the
"unavailable" path is exercised by the backend test and the proof, and the
smoke checks that frame labels agree with pass values.

`scripts/render_frame_report.py` validates the JSON shape and labels after
the render-graph reference comparison and prints the cold figures and warm
median/max, so each smoke log carries the report.

## Proof

`proof/frame_report.elisa` proves for `FrameReportGpu`: a negative GPU time
leaves the total unmeasured and unchanged, an unmeasured total is never over
budget, and negative CPU time or GPU time below -1 is refused (33/33
obligations). The prover does not substitute a negative literal argument into
callee ensures, so the harness uses `requires`-bounded parameters.

## Checks

- `test/backend_frame_report.elisa` exits 0 (codes 1-18) and is in
  `scripts/check.elisascript`.
- Render smoke group 240, see above.
- Negative control: letting an unavailable pass keep the GPU total "known"
  fails the backend test.

## Gaps

- Draws are visible objects and composed views, not API draw calls; Wicked
  does not expose a per-list draw-call counter.
- Upload bytes are new device allocations, not staging copies into existing
  buffers.
- Only three coarse passes are timed; Wicked's internal passes (shadows,
  post-process) are inside "scene".
- No Tracy capture file is produced by the gate.
- `scripts/check.elisascript` does not run the render smoke; run it with
  `PYTHONPATH=scripts python3 scripts/render_scene_native_smoke.py`. Its
  packaged-shader step denies reads under the checkout's parent, so a scratch
  worktree must sit beside `elisa-engine`, not under `/tmp`.
