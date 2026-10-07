# Animation-event sounds

Animation-marker sound policy and the Character Course integration. The
original binding and live-audio tests were validated on 2026-09-29; the
clip-authored path was added on 2026-10-06 and its queue consumer and spatial
mix were verified on 2026-10-07.

This is S05 progress alongside [footfall and landing triggers](movement-sound-triggers.md)
and [music transitions](music-transitions.md).

## Design

- **Policy.** `src/audio/anim_events.elisa` (`AudioAnimEvents`) binds up to
  four animation event ids (1–65535) to a sound event and gain in permille.
  The clip owns marker positions; the `.sfx` asset owns their sounds.
  `crossed_clip_event` compares integer clip ticks to cycle progress without
  rounding the marker to a milli-cycle. No motion crosses nothing, and a hitch
  of several cycles sounds at most once per update.
- **Clip authoring.** A glTF animation may put tick-aligned events in
  `animation.extras.elisaEvents`, for example `{ "id": 100, "time": 0.0 }`
  and `{ "id": 101, "time": 0.5 }`. Times are seconds and must land on the
  30 Hz cooked clip's ticks. The cooker writes event IDs and integer ticks to
  the existing `elisa-anim-v1` event records. It accepts up to 32 ordered
  markers per clip; repeated IDs are permitted.
- **Sound bindings.** `.sfx` uses `anim <animation event> <sound event> <gain>`.
  The sound event must be defined earlier (`UnknownEvent` otherwise). A zero
  or out-of-range animation id, sound event 0, gain over 1000 or fifth binding
  gives `InvalidAnim`; duplicate binding IDs give `DuplicateAnim`. Validation
  and reload are all-or-nothing with the rest of the asset.
- **Course dispatch.** The generated guide `lift` clip authors foot plants
  100 at tick 0 and 101 at tick 15. Walker creation decodes the packaged
  `.anim` file once and copies the bounded event table into walker state.
  Crossed ticks enqueue their IDs through `Anim::anim_emit_event`; the common
  `AudioAnimEvents::consume_state` maps the `AnimState.events` queue into a
  bounded dispatch batch and clears it once consumed. A full queue is drained
  in chunks. Invalid queue contents are left intact, while unbound IDs are
  consumed silently. `events.sfx` contains the sound bindings `anim 100 7 500`
  and `anim 101 7 500`; it does not duplicate marker positions. The frame path
  uses fixed arrays and allocates nothing.
- **Spatial mix.** Guide footfalls use the current player and guide physics
  poses with `SpatialAudio::spatial_gain`, the same distance attenuation policy
  used by `WorldAudio`. The omni source reaches zero gain at 18 scene units;
  its first voice frame receives the mix before the voice is stored. Doppler,
  occlusion and directional panning are not applied to these one-shot effects.

## Proof

`proof/audio_anim_events.elisa` proves guarded binding validity, the full-gain
clamp, and that both milli-cycle and exact clip-tick crossing imply progress.
`crossed_clip_event` uses a 30 Hz clip bound of 3600 ticks, matching the
cooker's 120-second limit.

## Focused checks (2026-10-06)

- `test/audio_anim_events.elisa` passed: binding table, gain clamp, and
  milli-cycle and exact clip-tick boundary, wrap, hitch and still-clip cases.
- `test/anim_state.elisa` passed, including bounded `ClipEvent` access and its
  invalid-index sentinel.
- `test/animation_cooked_contract.elisa` passed; it decoded event 100 at tick
  0 and event 101 at tick 15 from the course contract.
- `/opt/homebrew/bin/python3 scripts/test_animation_contract.py` and
  `scripts/gltf_skin_self_test.py` passed, including event validation,
  time-to-tick conversion, serialization and deterministic cooking.
- The focused Elisa Proof target proved with 9/9 obligations and no unproven
  goals. `scripts/check_source_length.py` and `git diff --check` passed.
- `character-course-smoke` passed with `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`
  and `SDL_AUDIODRIVER=dummy`. The hidden course self-test compiled and ran the
  integrated dispatch; its stress trace reported zero voices and streams.
  Audio output remained silent throughout.

## Queue consumer and guide mix (2026-10-07)

- `test/anim_state.elisa` passed with queue count, external event enqueue,
  invalid-ID rejection and explicit drain coverage.
- `test/audio_anim_events.elisa` passed with generated `AnimState` events,
  ordered binding dispatch, single consumption, unbound-event handling and
  malformed-queue preservation.
- `/opt/homebrew/bin/python3 scripts/application_native_smoke.py --only
  character-course-smoke` passed on the current source. The test traversed the
  cooked marker → `AnimState.events` → sound binding → spatial voice path; the
  final stress trace returned to zero voices and streams.
- The focused live mix check verifies near-source gain exceeds far-source gain
  and reaches zero at the configured 18-unit range. No audible listening test
  was run.

The 2026-09-29 live-audio check exercised sound-event deduplication, marker
crossings and `.sfx` reload before marker positions moved into the clip. No
audible listening test has been run for the clip-authored path.

## Remaining gaps

- No audible listening test was run for the guide footfalls or their distance
  attenuation.
