# Render frame report and budgets

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R20 progress.

## Design

`src/backend/frame_report.elisa` (`FrameReport`) accumulates per-pass CPU and
GPU time, draw counts, upload bytes and resource bytes into one `Report`, and
names the slowest pass.

- A GPU time of `Gpu::NONE` (-1) means the driver gave no timing. One such
  pass makes the frame's GPU total unavailable (`gpu_measured` is false); the
  other passes' CPU, draw and byte totals still sum. A caller prints
  "unavailable" rather than 0.
- `record` refuses negative counts or a GPU time below -1 and returns the
  report unchanged, so a bad probe cannot lower a total.
- `judge` checks a frame against a `Budget` in the fixed order cpu, gpu,
  draws, uploads, resources; an unavailable GPU total is never a failure.
  `judge_frame` uses a looser cold budget for frame 0 and the warm budget
  after it.

## Checks

- `test/backend_frame_report.elisa` exits 0 (codes 1-18): totals, slowest
  pass, unavailable GPU labelling, mixed measured/unavailable passes,
  rejected probes, each budget tripping, and cold versus warm judgement.
- Negative control: letting an unavailable pass keep the GPU total "known"
  fails the test.
- Wired into `scripts/check.elisascript`.

## Gaps

- No Tracy zones, plots or frame markers are emitted, and no real pass
  timings are fed in; the backend does not report per-pass GPU queries yet.
- No representative scene report or checked-in thresholds exist, so R20
  stays open.
- No proof written for this module.
