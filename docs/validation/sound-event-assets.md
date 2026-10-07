# Authored sound-event assets

Validated on 2026-09-28 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is S05 progress. It follows
[`course-audio-mix.md`](course-audio-mix.md), which listed code-constant events
and weights as a gap.

## Design

- **Format.** An asset is bounded text of at most 4096 bytes, one record per
  line (`src/audio/event_assets.elisa`):
  - `seed <n>` sets the board's variant seed;
  - `event <id> <group> <limit> <variants> <cooldown> [weight ...]` defines
    one event, with up to four weights.
  - `#` starts a comment anywhere, and blank lines are skipped. Fields are
    unsigned decimals separated by spaces or tabs.
- **Reader.** `src/audio/event_asset_text.elisa` holds the byte source and the
  record reader. It knows nothing about the event board, so the proof covers
  the whole reader.
  - A record reads at most nine numbers into a local array.
  - A number saturates at 10^18 and is then rejected as `NumberTooLarge`.
    Fields that must fit u32 are checked before the definition is built.
- **All or nothing.** `define_from` first defines the whole asset on a scratch
  board, and writes the caller's board only if that succeeds. A malformed
  asset never leaves a partial event set. `failing_line` names the first line
  it rejects, for authoring errors.
- **Native read.** `AudioRuntime::read_asset` (`elisa_audio_v1_read_asset`)
  reads a small file into caller storage. It needs no audio service.
  - A missing file is `DecodeFailed`.
  - A file longer than the capacity is `Capacity`.
- **Course.** The character course's events, groups, cooldowns, jump weights
  and seed now live in `examples/character_course/sounds/events.sfx`.
  - The code-constant `define_all` is gone.
  - A missing or malformed file fails the audio start instead of falling back
    to code.

## Proof

`proof/sound_event_assets.elisa` is proved: 90 of 90 certificates replayed,
with no gaps. It shows that:

- `skip_spaces`, `token_end` and `newline_in` never leave the line they were
  given;
- `line_end` stays inside the source;
- a record never has more than nine fields, so every `fields[count]` write is
  in bounds;
- a decimal field stays below the saturation cap, with no unsigned overflow on
  the way.

This needed eleven prover fixes in `../elisa-engine-proof`, listed in its
`AUDIT.md` entry "Loop states across rebinds, arm locals and aggregate calls".
Examples of the fixes:

- conditional-binding ranges;
- alias transfer;
- negated-order replay;
- joins over rebinding arms and arms that declare locals;
- aggregate call-summary rebinding.

## Checks

`test/audio_event_assets.elisa` runs in `scripts/check.elisascript` with the
other audio policy tests.

| Code | Check |
|---|---|
| 1–5 | the course's asset shape loads with no failing line: comments, a seed, weights, fired events, a 6-tick land cooldown, an unknown event |
| 10–15 | unknown record word, too few and too many fields, letters, a u32 overflow and a saturated number are each rejected with their own error |
| 16–17 | an invalid definition and a duplicate id are rejected; the duplicate's failing line is 2 |
| 19–20 | an event past the board's capacity is rejected at line 17 |
| 21 | weights beyond the variant count are rejected |
| 22–23 | a source fills to exactly 4096 bytes, refuses one more byte and still loads |
| 30–32 | a rejected asset leaves an already loaded board unchanged |

In the course self-test, code 177 now also starts audio with
`sounds/missing.sfx` and expects `DEFINITION_FAILED`. The course and relaunch
smokes load the real asset.

Commands:

```
ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/audio_event_assets-test test/audio_event_assets.elisa && build/audio_event_assets-test
../elisa-engine-proof/build/elisa-proof --json proof/sound_event_assets.elisa
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
```

## Negative controls

All controls were reverted.

- **The field-count check before the array write is removed.** The proof
  fails in `read_fields`.
- **The `NumberTooLarge` check is removed.** The proof fails in `number`,
  `prove_number_capped` and `prove_fields_bounded`.
- **`newline_in` returns one past the newline.** The proof fails in
  `newline_in`, `line_end` and `prove_line_end_in_source`.
- **`define_from` skips its scratch pass.** The test fails at 32: the broken
  asset's first record reaches the caller's board.

## Gaps

- Mixer snapshots are still code constants, not asset records.
- Footfall, animation and physics triggers are still open.
- There is no hot reload. The asset is read once when audio starts.
- The proof covers the text reader, not `define_line`'s use of the board;
  only the test covers that part.

## Current remaining replay gaps

Prover `65b58b71` now checks 157 obligations and replays 155, with no source findings or semantic errors. The two gaps are goal 14 in number (the loop-rebound result <= 10^18) and goal 156 in prove_number_capped (the dependent number summary < 10^18). This supersedes the earlier four-gap baseline. Preserve the exact capped decimal loop semantics while repairing source binding/replay; the producer has certificates for all 157, so producer success alone is insufficient. Report: `build/validation/sound_event_assets-current.json`.
