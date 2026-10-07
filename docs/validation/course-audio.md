# Character course audio

Queue item 6 asks for the running client's audio path: streamed music,
impacts and ambience that survive pause, scene replacement and shutdown;
event sounds that fire once; reusable released resources; callbacks with no
blocking IO or allocation; and measured underruns and memory.

## Design

- **Streaming (`native/miniaudio_stream.h`).** At most two streams, each with a
  fixed ring of 500 ms. The ring is allocated once when the device is first
  initialized and reused by later streams. The owner thread decodes into the
  ring from `AudioRuntime::pump_streams`, which `RuntimeServices::pump` calls
  once per frame. The device callback only copies frames out. `written` and
  `consumed` are atomic frame counters. A starved request plays silence and is
  counted in `underrun_frames`. The callback takes the service lock with
  `try_lock`. If the lock is held, it emits silence and counts the callback in
  `contended_callbacks` rather than waiting. The test-only `mix_for_test`
  still waits for the lock. When it shared the callback's `try_lock` path, the
  Wicked probe's reopen check intermittently read silence from a running null
  device. Stopping a stream releases its
  decoder and slot at once, and the old handle becomes stale.
- **Buses.** `set_bus_paused` holds a bus's voices and streams in place, and
  resuming continues from the same sample. `recover_device` reopens after
  device loss: streams keep playing, while one-shot voices end.
  `RuntimeServices` reports the resulting route.
- **Events (`src/audio/events.elisa`, `AudioEvents`).** This module is pure
  policy with bounded tables: 16 definitions, 32 instances and 8 groups. An
  event fires at most once per simulation tick and then honours its cooldown.
  When a group reaches its limit, a new trigger replaces the group's oldest
  instance. Variants rotate in order. `reset` forgets the scene's history.
  `test/audio_events.elisa` runs in `scripts/check.elisascript`. The error
  type is named `SoundEventError` because stage1 rejects the whole
  application build when two modules declare an error with the same bare
  name.
- **Course (`examples/character_course/sounds.elisa` and
  `examples/character_course/sound_spatial.elisa`).**
  - The course plays looped music on the Music bus (streamed) and a looped
    wind ambience on Effects.
  - Jump has two variants. Land has a 6-step cooldown. Win, fall and save
    share one group, so each result cuts off the previous one. Save plays on
    the Ui bus because it can fire from the pause screen.
  - Pause holds Effects, while music and Ui keep playing.
  - Restart and load call `reset_scene`. `stop` releases every voice, the
    stream and all clips.
  - Clip-authored guide footfalls pass through the generic `AnimState.events`
    consumer. The one-shot voices receive distance attenuation from the live
    player and guide poses through `SpatialAudio`; the current mix is omni and
    does not apply Doppler or occlusion. See
    [animation-event sounds](animation-event-sounds.md).
  - The course requests the audio service with `MiniaudioDefault`, which
    falls back to the silent route when no device is available. If a clip
    cannot load, the game runs without sound.
  - The package ships `sounds/` through `package.resources`.
- **Assets.** `make_sounds.py` synthesizes the WAV files from sine tones and
  seeded noise, so no third-party audio is included. CI runs `--check` as the
  `course-sounds` stage. The step fails if a committed file drifts from the
  script's output.

## Self-test (`character-course-smoke`)

`sound_test` runs after the controls test, on the application's real audio
device:

| Code | Check |
| --- | --- |
| 90 | the sounds start |
| 91 | a second jump in the same tick is `Duplicate` |
| 92 | jump replaces its own voice; land plays, then cools down; fall replaces win |
| 93 | Effects pauses and resumes; 1.5 s of ordinary frame pumping completes |
| 94 | music is still `Playing` and has advanced |
| 95 | music reported zero underrun frames over those 1.5 s (three times the ring) |
| 96 | a scene reset leaves no live event voices, and a jump in tick 1 plays again |
| 97 | `stop` leaves zero voices and zero streams in the service |
| 98 | a second `start` reuses the freed slots and plays save |

## Results (2026-09-27, Apple Silicon, macOS 27)

| Case | Result |
| --- | --- |
| `application_native_smoke.py --only character-course-smoke` | pass |
| Negative control (check 95 inverted to `== 0`) | smoke failed with status 95, so the underrun check runs and measured zero |
| Stream harness, ASan + UBSan (`run_boundary_sanitized.py`) | pass; frames=3280, underruns=0, contended=0 |
| Stream harness, TSan | pass; frames=3440, underruns=0, contended=0 |

The harness covers these cases:

- a missing file is `DecodeFailed`
- prefilled and refilled frames match the source sample for sample
- a paused bus is silent and holds its position
- a one-shot stream finishes
- a stale handle is rejected and its slot is reused
- the capacity limit
- seamless looping with zero underruns
- stream gain
- a stream keeps playing across `reopen_null` device recovery with zero underruns

**Memory (computed from the formats, not measured with RSS):**

- **Stream rings:** 500 ms × 48 kHz × 2 channels × 2 bytes = 96,000 B per
  stream, or 192,000 B for both slots.
- **Decoded clips at 48 kHz stereo:** 1,094,400 B in total. That is
  768,000 B of ambience; 96,000 B of fall; 26,880 B each for jump_a and
  jump_b; 23,040 B of land; 30,720 B of save; and 122,880 B of win.
- **Music:** the 21.33 s track would take 4,096,000 B decoded, so streaming
  it saves about 3.9 MB.

## Gaps

- Nobody has listened to the result yet. Mix levels and timing need manual
  play.
- Device recovery is tested only through the null backend (`reopen_null`). No
  real output device was unplugged.
- Memory figures are computed, not measured with RSS.
- No person has listened to the shipped mix or checked its distance falloff.
- Physical device unplug/reconnect and measured memory on the running workload
  remain open. `WorldAudio` device-independent policy is covered separately.
