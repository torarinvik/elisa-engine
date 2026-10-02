# Audio regression assertions

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is S07 progress.

## Design

`src/audio/regression.elisa` (`AudioRegression`) makes offline-mix and
timing checks deterministic and countable.

- `Stats` is fed a mix sample by sample and tracks count, peak, energy,
  clipped samples (at the rail or out of range) and silent samples. It stops
  at 100,000,000 samples so the energy sum cannot overflow. `clean` needs no
  clipping and not all silence.
- `apply_gain` (permille, rounded toward zero), `downmix` (stereo to mono)
  and `seam_ok` (last versus first sample of a loop within a limit) give the
  gain, channel-mapping and looping assertions.
- `Timing` records each callback's duration against its budget and frames
  delivered against requested. Overruns, underruns, short frames and the
  worst callback are counts, so a long run exposes them; bad probes are
  ignored and an empty run is not healthy.

## Checks

- `test/audio_regression.elisa` exits 0 (codes 1-15): statistics, clipping,
  silence, gain, downmix, seams, overruns, underruns and rejected probes.
- Negative control: not counting underruns fails the test (exit 13).
- Wired into `scripts/check.elisascript`.

## Gaps

- The assertions are not yet run against the real mixer output or the miniaudio
  callback; no long-session run has produced a report. S07 stays open.
- Spatial fixtures: `test/audio_spatial_regression.elisa` renders a ±20000
  square wave through `SpatialAudio::spatial_gain` and asserts mix stats:
  peaks of 18000 at 10 units and 10000 at 50 units (range 100), silence past
  range, 2500 behind the cone (outer gain 0.25) and 5000 at half occlusion.
  Gain uses the portable model, not the native mixer's output.
- A proof of `absolute` and `limit` was tried and dropped: the prover could not
  establish the clamp helper's ensures (returns of `32767` and `0 - 32767` after
  guards), so nothing here is proved.
