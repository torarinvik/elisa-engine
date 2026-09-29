# Movement sound triggers

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is S05 progress. It follows
[`sound-event-assets.md`](sound-event-assets.md), which listed
animation/physics triggers as a gap.

## Design

- **Policy.** `src/audio/triggers.elisa` (`AudioTriggers`) works in whole
  millimetres, so a replayed movement produces the same triggers.
  - `Footfalls` adds each tick's ground travel. It reports at most one
    footfall per tick and drops travel past a whole stride, so a teleport
    does not burst steps. Leaving the ground forgets the partial stride.
  - `Impacts` tracks the peak height while airborne. On the first grounded
    tick it returns a permille gain: 0 below the threshold, then rising
    linearly from 250 to 1000 at the full drop. Drops are clamped to 1 km.
  - `reset` forgets partial strides and falls on scene replacement or
    restart.
- **Units.** `src/audio/trigger_units.elisa` converts metres to
  millimetres, with saturation, and is exported by `runtime/public.elisa`.
- **Authoring.** The `.sfx` asset gains two records
  (`src/audio/event_assets.elisa`):
  - `footstep <event> <stride_mm>`
  - `impact <event> <threshold_mm> <full_mm>`

  A trigger names an event defined earlier in the asset (`UnknownEvent`),
  must pass `footfalls_valid`/`impacts_valid` (`InvalidTrigger`), and may
  appear once per kind (`DuplicateTrigger`). `define_from` fills a
  `SoundEventAssets::Triggers` all or nothing, in the same way as the board.
  An asset without trigger records clears them.
- **Course.** `CourseSounds::track` runs every committed pose through both
  triggers. It fires the footfall event (`sounds/footstep.wav`, event 6,
  captioned "[Sound: footstep]"), and fires the landing event scaled by the
  gain. `sounds/events.sfx` authors `footstep 6 650` and
  `impact 2 300 2400`. A short hop now lands silently, although the landing
  caption still shows on every touchdown.

## Proof

`proof/audio_triggers.elisa` proves the following:

- gain stays within 1000;
- the drop stays within `MAX_DROP_MM`;
- strides stay in range;
- travel stays below two strides;
- after the asset reader's guard, both constructors' requires hold.

The last proof needed explicit `ensure not result or (...)` contracts on the
two `*_valid` predicates. The prover summarises a call only from its explicit
ensures. Removing the footfall ensure turns `prove_guarded_footfalls` into
`call-requires-unproven`.

`define_trigger` itself is not proved. It lives in `event_assets.elisa`,
which includes `events.elisa`, and the prover still reports open index and
fact-budget findings there. A probe with the same `u64[M::N]` indexing and
raises in an `extend` block proves in isolation.

## Checks

- `build/aea-test` (`test/audio_event_assets.elisa`) exits 0. Codes 40–52
  cover:
  - accepted triggers and their bound fields;
  - a trigger before its event (UnknownEvent, line 1);
  - a short stride and a flat impact (InvalidTrigger);
  - a duplicate impact (DuplicateTrigger, line 7);
  - a missing field;
  - rejected assets leaving triggers intact;
  - plain assets clearing them.
- Negative control: disabling the duplicate-impact check makes it exit 47.
- `test/audio_triggers.elisa` passes. The course self-test (code 195) covers:
  - three ticks at 0.8 m giving a step;
  - a 0.2 m drop staying silent;
  - a 2 m drop giving a scaled landing.
- `scripts/application_native_smoke.py --only
  character-course-smoke,character-course-relaunch-smoke` passes.
- The prover reports `proof/audio_triggers.elisa` as proved.

## Gaps

- The prover does not derive guard facts from a bool predicate's body. It
  needs an explicit ensure (see the prover backlog).
- `define_trigger` and the rest of `event_assets.elisa` are not yet proved.
- Triggers come only from movement. Animation-event triggers, mixer
  snapshots as assets, and hot reload remain.
- No audible listening test was run in this session.
