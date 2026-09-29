# Voice capture consent and jitter buffer

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is S06 progress.

## Design

`src/audio/voice.elisa` (`AudioVoice`) is policy only: no microphone, Opus or
network code.

- **Consent.** `start` succeeds only after `enable(true)`. Loading a scene
  changes nothing. A device change stops capture until it is started again,
  disabling stops it, and `sending` is false while muted or stopped.
- **Jitter buffer.** Frames are keyed by sequence number in a window of eight
  and played strictly in order. Reordered frames within the window are put
  back in order. A hole is played as `Concealed` (the decoder conceals it)
  and counted lost. A frame that arrives after its slot has passed, or a
  repeat, is refused (late frames are counted). A frame beyond the window
  advances it, counting every skipped frame as lost.
- **Bandwidth.** `within_bandwidth` checks frame bytes and duration against a
  bits-per-second cap (80 bytes per 20 ms is 32 kbit/s).

## Checks

- `test/audio_voice.elisa` exits 0 (codes 1-21): implicit start, scene load,
  mute, device change, disable, ordering, repeats, reordering, concealment,
  late frames, window jumps and bandwidth limits.
- Negative control: letting `start` ignore consent fails the test (exit 2).
- Wired into `scripts/check.elisascript`.

## Gaps

- No capture device, Opus encoder/decoder, RNNoise or network transport is
  bound, and no two-process run measured latency or packet loss. S06 stays
  open.
- The eight-frame window and any target playout delay are untuned.
- No proof written for this module.
