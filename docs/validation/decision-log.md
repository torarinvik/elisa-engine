# Seed and decision recording

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`, because another session had newer
compiler sources than the binary). This is W09 progress: the seed and
decision recording that `replay-subsystems.md` listed as remaining.

## Design

`src/runtime/decision_log.elisa` (`RuntimeDecisionLog`):

- **Recording.** `draw(tick, site, bound)` takes values from a per-run
  xorshift64 generator and logs `(tick, site, value)`. `decide` logs choices
  made outside the generator, such as an AI pick. The log holds 256 entries;
  past that, `overflow` is set instead of dropping entries silently.
- **Replaying.** `replayer` serves the recorded values back. The first
  request whose tick or site differs from the log, or that asks past its
  end, sets `diverged_at` to that tick. Later draws return -1.
- `complete` requires every entry consumed, no divergence and no overflow.

## Checks

- `test/runtime_decision_log.elisa` exits 0. It covers:
  - a 50-tick game with a damage roll and an AI choice per tick;
  - the same seed giving the same run, and a different seed differing;
  - a faithful replay that is complete;
  - a site change caught at tick 17;
  - an over-long run caught at tick 50;
  - an early stop that is not complete;
  - draw bounds, the capacity flag, and an overflowed log never being
    complete.
- Negative control: ignoring the site id makes it exit 5.
- Wired into `scripts/check.elisascript`.

## Durable form

`src/runtime/decision_log_codec.elisa` (`RuntimeDecisionLogCodec`) encodes a
log as a versioned word stream: magic, version, seed, count, overflow flag,
entries, then a checksum. `decode` checks the length, magic, version, count,
flag and checksum before it returns a replayer.

`test/runtime_decision_log_codec.elisa` exits 0. It covers:
- a 40-tick round trip that replays completely;
- a flipped entry;
- truncation;
- a foreign magic number and an unknown version;
- an impossible count;
- an empty log.

Negative control: skipping the checksum makes it exit 4.

## Gaps

- Not yet wired into the runtime frame or any game. The word stream is not
  yet written to disk beside `native/replay_trace.h` traces.
- There is no per-subsystem split of sites into `ReplaySubsystems` digests.
