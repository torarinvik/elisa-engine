# Voice virtualization

S02 asks for virtualization: a sound that loses its native voice to the
budget keeps its place in time and returns where it would have been. Before
this slice a stolen voice was simply gone, and the course ambience stayed
silent after an event voice stole it.

## Design

- **Native (`native/miniaudio_service.h`, `audio_service_abi`).**
  - `seek_voice(voice, frame)` moves a live voice's cursor on the owner thread
    under the service lock. It refuses a frame at or past the clip's length.
  - `voice_frame(voice)` reads the cursor, and `clip_frames(clip)` reads a
    clip's length.
  - The ABI calls are `elisa_audio_v1_seek_voice`, `elisa_audio_v1_voice_frame`
    and `elisa_audio_v1_clip_frames`. A dead voice reports `INVALID_HANDLE`,
    and a refused frame reports `INVALID_ARGUMENT`.
  - `AudioRuntime` wraps them as `seek_voice`, `voice_frame` and
    `clip_frames`.
  - The callback does no new work: seeking only rewrites fields it already
    reads under the same lock.
- **Policy (`src/audio/virtual.elisa`, module `AudioVirtual`).** A fixed pool
  of 32 sounds. Each sound records its priority, whether it loops, its length
  in frames and its start time. It needs no allocation and no native handles.
  - A sound starts virtual. `realized` and `virtualized` mark it real or
    virtual.
  - `position(now, rate)` gives the frame the sound would be at now. A loop
    wraps and a one-shot clamps to its last frame.
  - `ended` reports a one-shot that has passed its length.
  - `next_virtual` picks the sound to bring back first: the highest priority,
    and the oldest among equals. Ended sounds are skipped.
- **Course (`examples/character_course/sounds.elisa`).**
  - The ambience loop is registered in the pool at start.
  - `update` runs once per frame after `mix`. When `voice_frame` refuses the
    ambience voice, the sound is marked virtual and `realize_ambience` tries
    again: it plays the loop, seeks it to the virtual clock's position and
    marks it real.
  - A refused realization leaves the sound virtual until a later frame.

## Checks

The stream harness (`native/miniaudio_stream_harness.cpp`,
`voice_seek_checks`) checks each case:

- The clip reports its length, and a new voice starts at frame 0.
- A seek to frame 12345 plays the source from there, and after 1000 frames the
  voice reports 13345.
- A seek 500 frames before the end of a loop wraps to frame 500.
- A seek past the clip's end is refused.
- A stopped voice is refused and reports no frame.
- A released clip reports length 0.

`test/audio_virtual.elisa` (codes 1–60) covers:

- Capacity, invalid lengths and unknown slots.
- Priority order and oldest-first ties in `next_virtual`.
- Loop wrapping, one-shot clamping and ended one-shots.
- Slot reuse after `remove`, and the real and virtual counts.

The course self-test, code 194 (after the scene reset in the sound test),
fails in each of these cases:

- After a reset, `update` does not report a real ambience.
- With the Effects budget at 1, a landing event does not play, or the
  ambience is not reported virtual after it steals the ambience's voice.
- After the next scene reset frees the budget, `update` does not bring the
  ambience back.
- The realized voice is more than 4800 frames (100 ms), measured around the
  loop, from where the virtual clock puts it.

The Effects budget is then restored to 32.

## Commands

```
python3.14 -c "import sys; sys.path.insert(0,'scripts'); import run_boundary_sanitized as r; print(r.run_stream_harness('address,undefined')); print(r.run_stream_harness('thread'))"
../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o $SP/audio_virtual test/audio_virtual.elisa && $SP/audio_virtual
bash $SP/dbg.sh     # course self-test under lldb
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

On 2026-09-28 the harness passed under both ASan/UBSan and TSan, and the
policy test exited 0. The course self-test exited 0 under lldb. The course and
relaunch smokes, `check` (`failed: 0`) and the native gate each exited 0.

## Negative controls

- With the seek bound loosened to `frame >`, the harness failed "a seek past
  the clip end is refused".
- With priority ignored in `outranks`, the policy test failed at code 10.
- With loops treated as one-shots in `position`, the policy test failed at
  code 23.
- With `realize_ambience` skipping the seek, so the loop restarts at frame 0,
  the course self-test failed at 194.

Each file was restored, and `cmp` confirmed it matches the verified copy.

## Gaps

- Only the course ambience is driven. A generic driver that owns many clips
  would need `AudioRuntime` to hold the clip and voice handles, because these
  opaque handles cannot be stored in arrays outside it.
- The virtual clock keeps running while its bus is paused. A paused ambience
  that is stolen comes back ahead of where it stopped.
- The course brings back at most one sound, so a refused realization simply
  waits for the next frame. With many sounds, a refusal should end that
  frame's pass through `next_virtual`.
- Nobody has listened to a realized ambience in manual play.
