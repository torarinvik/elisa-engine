# Per-subsystem replay trace codec

- `src/runtime/replay_subsystems_codec.elisa` (`ReplaySubsystemsCodec`) turns a
  `ReplaySubsystems::Trace` into a word stream: magic, version, tick count, six
  digests per tick, then a checksum. The stream packs to at most 800
  little-endian bytes, within the user-data payload limit.
- `decode_trace` rejects truncated, foreign, newer-version, over-long and
  corrupt records before producing a trace.
- `test/runtime_replay_subsystems_codec.elisa` exits 0 (codes 1–10):
  - an exact round trip, including negative 64-bit digests, that still judges
    Match
  - checksum, truncation, magic, version, tick-count and ragged-byte refusals
  - an empty trace
- It is registered in `scripts/check.elisascript`.
- Control: removing the checksum comparison exits 4.
- Remaining: saving the course's traces through UserData, and recording
  during live play.
