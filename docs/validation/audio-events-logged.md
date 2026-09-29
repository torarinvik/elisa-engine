# Sound events through the decision log

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is W09 progress. It follows
[`decision-replay.md`](decision-replay.md), which noted that no shipped
system drew through the log yet.

## Design

`src/audio/events_logged.elisa` (`AudioEventsLogged`) wraps
`AudioEvents::trigger`. Sound events are the engine's shipped system that
makes random choices: weighted variants and voice stealing.

- `seed_from` seeds the board from the log's run seed, so recording and
  replay roll the same weighted variants.
- `trigger` fires the event. When a voice starts (Play or Replace), it
  records one decision at site `4096 + event id`. The decision folds the
  outcome, the instance slot and the variant into one value.
  - While replaying, the decision is checked instead. A different variant
    or a different stolen voice marks the tick as divergent, and `matched`
    turns false.
  - Duplicate and cooling triggers start nothing and log nothing.

## Checks

`test/audio_events_logged.elisa` exits 0. It fires a 4-variant,
1:2:3:4-weighted, two-voice event every third tick for 60 ticks.

| Code | What it checks |
| --- | --- |
| 2–4 | The recording logs 20 decisions, the last at tick 57. |
| 11 | A duplicate trigger on tick 57 logs nothing. |
| 5–6 | A fresh board replaying the log matches every trigger, and the replay is complete. |
| 7–9 | A board re-seeded to 999 mismatches. The replay names a trigger tick and is not complete. |
| 10 | Variants 1, 2 and 3 all appear. |
| 12–13 | A run seeded 777 records a different variant sequence, showing that the board takes its seed from the log. |

Negative controls:

- Dropping the variant from the logged value exits 8.
- Seeding the board with a constant instead of the log's seed exits 13.

The source-length check passes, and the test is wired into
`scripts/check.elisascript`.

## Gaps

- The course game does not yet call `AudioEventsLogged`. The wrapper is
  exercised only by the headless test.
- No other randomised system exists yet to route through the log.
