# Authored mix moods

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is S05 progress. It follows
[`movement-sound-triggers.md`](movement-sound-triggers.md), which listed
mixer snapshots as assets as a gap.

## Design

- **Policy.** `src/audio/moods.elisa` (`AudioMoods`) holds four moods by id.
  Each mood has music, effects and UI gains in permille (at most 1000) and a
  ramp in milliseconds (at most 3600, the mixer's ramp limit). A mood the
  asset leaves out is heard at full gain after the default 250 ms ramp, so a
  game without authored moods mixes as before. `heard_permille` and
  `heard_ramp_ms` clamp whatever a hand-built mood holds.
- **Authoring.** `.sfx` assets gain `mood <id> <music> <effects> <ui>
  <ramp_ms>` (`src/audio/event_assets.elisa`). A record must have five fields
  (`FieldCount`), pass `AudioMoods::valid` (`InvalidMood`) and name each id
  once (`DuplicateMood`). Moods live in `SoundEventAssets::Triggers` and are
  filled all or nothing with the rest of the asset; an asset without moods
  clears them.
- **Course.** `CourseSounds` maps play, pause and menu to moods 0–2 and reads
  their gains and ramps from `sounds/events.sfx` (`mood 0 1000 1000 1000
  250`, `mood 1 450 300 1000 250`, `mood 2 200 300 1000 250`). The hard-coded
  ducking values and ramp were removed.

## Proof

`proof/audio_moods.elisa` proves the following (status proved, 20/20
certificates replayed):

- a record that passes `valid` meets `mood`'s requires and has an id below
  `MAX_MOODS`;
- the heard gain stays within 1000;
- the heard ramp stays within `MAX_RAMP_MS`.

The ramp proof takes a `Mood` parameter. Written over a struct construct
passed straight to a call (`heard_ramp_ms(Mood{...})`), the goal proves but
fails kernel replay. That prover hole is queued for `elisa-engine-proof`.

## Checks

- `build/aea-test` (`test/audio_event_assets.elisa`) exits 0. Codes 60–69
  cover:
  - accepted moods, their values and the defaults for unauthored ids;
  - id 4, a gain of 1001 and a ramp of 3601 (InvalidMood);
  - a duplicate mood (DuplicateMood, line 7);
  - a missing field (FieldCount);
  - a rejected asset keeping moods;
  - a plain asset clearing them.
- Negative control: disabling the duplicate-mood check makes it exit 66.
- The course self-test checks that an unloaded menu mood is at full gain and
  that the authored moods and ramps apply after `define_assets`.
- `scripts/application_native_smoke.py --only
  character-course-smoke,character-course-relaunch-smoke` passes.

## Gaps

- Moods load once at start; hot reload remains.
- Music transitions and animation-event triggers remain.
- No audible listening test was run in this session.
