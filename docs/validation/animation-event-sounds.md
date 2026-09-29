# Animation-event sounds

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is S05 progress and closes its trigger list: footfalls and landings
([`movement-sound-triggers.md`](movement-sound-triggers.md)), music
([`music-transitions.md`](music-transitions.md)) and now animation markers.

## Design

- **Policy.** `src/audio/anim_events.elisa` (`AudioAnimEvents`) binds up to
  four animation event ids (1–65535) to a sound event and a gain in permille.
  The clip owns where its markers sit; the asset owns what each marker sounds
  like. `crossed(before_mc, after_mc, marker_mc)` says whether a clip that
  advanced from `before_mc` to `after_mc` thousandths of a cycle passed a
  marker. A marker exactly at the start was heard on the previous update, no
  motion crosses nothing, and a hitch of several cycles sounds once per
  update, so a replayed animation repeats the same sounds.
- **Authoring.** `.sfx` gains `anim <animation event> <sound event> <gain>`.
  The sound event must be defined on an earlier line (`UnknownEvent`
  otherwise). A bad id, sound event 0, a gain over 1000 or a fifth binding
  gives `InvalidAnim`; a repeated animation event gives `DuplicateAnim`. The
  record is validated all or nothing with the rest of the asset and reloads
  with it.
- **Course.** The guide's walk clip plants a foot at the start of a cycle
  (animation event 100) and half-way (101). `CourseSounds::animation_markers`
  takes the cycle position before and after each update
  (`walker.cycles` in milli-cycles), fires each marker passed through
  `fire_animation`, and returns how many were heard. `events.sfx` authors
  sound event 7 (the guide's footstep, its own concurrency group) and
  `anim 100 7 500` / `anim 101 7 500`. An unbound marker is silent.

## Proof

`proof/audio_anim_events.elisa` proves that a guarded record meets
`binding`'s requires and names an id within range, that any binding's heard
gain is at most full, and that `crossed` implies the position advanced.

`crossed` needed an explicit early return for a still clip: the prover does not
yet reason that floor division is monotone.

## Checks

- `test/audio_anim_events.elisa` exits 0: validation, table fill and lookup,
  full table, marker crossings (exact start, wrap, hitch, no motion, once
  per cycle over 4 cycles) and the gain clamp.
- `test/audio_event_assets.elisa` codes 80–91: accepted records, a zero id, an
  over-full gain, sound event 0, an unknown sound event, a duplicate (line 7),
  a fifth binding, a short record, a rejected asset keeping bindings and a
  plain one clearing them.
- Negative control: disabling the duplicate-animation check makes the asset
  test exit 87.
- Course self-test 200 (live audio): no marker before the half cycle, the half
  cycle sounds, a repeat in the same tick does not, a cycle end sounds, two
  markers in one hitch sound once (same event, same tick), and a reload
  without the bindings is silent.
- `character-course-smoke` and `character-course-relaunch-smoke` pass.

## Gaps

- The markers' positions (0 and half a cycle) are course constants; the
  skinned rig's clip asset (`ClipEvent` in `src/animation/assets.elisa`) does
  not yet carry them into `anim` records, and `Anim::AnimState` events are not
  consumed.
- Sounds are not spatialised to the guide.
- No audible listening test was run in this session.
