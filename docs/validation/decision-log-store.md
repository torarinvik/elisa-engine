# Decision log storage

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is W09 progress and follows
[`decision-log.md`](decision-log.md), which listed disk wiring as a gap.

## Design

- `RuntimeDecisionLogCodec` gains a byte form. `pack` writes each stream word
  as 8 little-endian two's-complement bytes (at most 6192 bytes). `unpack`
  reverses it and returns an empty stream, which decodes as `Truncated`, for
  a partial word.
- `src/runtime/decision_log_store.elisa` (`RuntimeDecisionLogStore`) saves a
  log as a versioned user-data payload. That payload uses the native layer's
  atomic replacement and checksum envelope.
- `load` maps storage failures to `NotFound`, `WrongVersion`, `Corrupt` and
  `StorageFailure`. Whatever bytes come back still go through `decode`, so
  a stale or foreign file never starts a replay.

## Checks

- `test/runtime_decision_log_codec.elisa` exits 0. New codes 10–13 cover:
  - negative and wide words surviving the byte form exactly;
  - a packed log decoding into a replayer that reproduces the run;
  - a partial word being refused as `Truncated`.
- Negative control: dropping the sign handling of the top byte in `unpack`
  makes the test trap with exit 133. The unsigned top byte overflows the
  signed accumulator.
- `test/user_data_probe.elisa`, codes 49–53, runs in
  `scripts/application_native_smoke.py --only quality-settings-native-smoke`,
  which passes. It covers:
  - recording 30 draws;
  - saving them to the user-data directory and reloading them;
  - replaying them identically, with the replayer complete;
  - removing the file, after which a load reports `NotFound`.

## Gaps

- No runtime system records its randomness through the log yet, and no
  replay tool loads it next to the native replay trace.
