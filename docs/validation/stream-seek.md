# Stream seek and position

S02 asks for seek/loop on streamed assets. Before this slice a stream could
loop, stop and pause, but it could not jump or report where it was. Restarting
the character course therefore left the music wherever it was.

## Design

- **Native (`native/miniaudio_stream.h`).** `open` records the decoder's length
  in output frames. `StreamTable::seek(handle, frame)` runs on the owner
  thread:
  1. It clears `live` under the service lock, so the callback stops reading.
  2. It drops the buffered ring by setting `written = consumed`.
  3. It records `seek_frame` and `seek_consumed`, seeks the decoder, refills
     the ring, and sets `live` again under the lock.

  The callback never waits on a decoder seek or on file IO. A decoder seek
  failure closes the stream and reports `DecodeFailed`.
- **Position.** `position = seek_frame + consumed - seek_consumed`. A looped
  stream wraps it modulo the length, and a one-shot stream clamps it to the
  length. It is reported in `StreamStatus`.
- **ABI (`audio_service_abi`).**
  - `elisa_audio_v1_seek_stream(slot, gen, frame)` maps each result:
    - Seeked → OK
    - Ended → `STREAM_ENDED` (-9)
    - OutOfRange → `INVALID_ARGUMENT`
    - DecodeFailed → `DECODE_FAILED`
    - Invalid → `INVALID_HANDLE`
  - `elisa_audio_v1_stream_position` reads the position.
- **Elisa (`src/audio/runtime.elisa`).**
  - `seek_stream(stream, frame) -> void error[AudioError]`
  - `stream_position(stream) -> u64 error[AudioError]`
  - A new `AudioError.StreamEnded`.
- **Course.**
  - `CourseSounds::rewind_music` seeks the music to frame 0, and the play loop
    calls it on R restart. Loading a save keeps the music playing.
  - `music_position` reads the position back.

## Checks

The stream harness (`native/miniaudio_stream_harness.cpp`) checks each case:

- A finished one-shot stream refuses a seek with Ended.
- A looped seek to frame 12345 reports that position, and after 1000 more
  frames it reports 13345 and plays the source from there.
- A seek 500 frames before the end wraps to position 500 after 1000 frames.
- A frame at or past the length and a stale handle are both refused.
- No seek causes an underrun.
- On a running device, a seek to frame 100 plays on with
  100 < position < 100 + one second, with no underruns.

The course self-test, code 193 (between `SoundCodes::UNDERRUN` and
`RELEASE`), fails in each of these cases:

- After 1.5 s of pumping, the music has not passed 12000 frames, or the
  rewind is refused.
- The music is still at or past 12000 frames after the rewind.
- A seek to `u64` max is not refused with `InvalidArgument`.
- After `stop`, the old music handle is not refused with `InvalidHandle`.

## Commands

```
python3.14 -c "import sys; sys.path.insert(0,'scripts'); import run_boundary_sanitized as r; print(r.run_stream_harness('address,undefined')); print(r.run_stream_harness('thread'))"
bash $SP/dbg.sh     # course self-test under lldb
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

On 2026-09-28 the stream harness passed under both ASan/UBSan and TSan. The
course self-test exited 0 under lldb, and the course and relaunch smokes and
the native gate each exited 0. The first `check` run exited 126; the rerun
completed with `failed: 0`.

## Negative controls

- Keeping the buffered ring across a seek (removing `written = consumed`) made
  the harness fail two checks: "a seek plays on from the target frame" and "a
  looped seek wraps across the end".
- A `rewind_music` that reads the position instead of seeking made the course
  self-test fail at 193.

Both files were restored, and `cmp` confirmed they match the verified copies.

The first version of the 193 check wrote its typed catch arms as
`error AudioRuntime::AudioError.InvalidHandle: 2`. With that form, stage1
sends every error to the first typed arm, so the stale handle read as
`InvalidArgument`. The bare arm form (`AudioRuntime::AudioError.InvalidHandle:
2`) matches correctly. A scratch repro confirmed this, and the existing
prefixed uses in `src/world/world.elisa` and a render test are left for a
separate fix.

## Gaps

- A seek is sample-accurate only as far as the decoder is. WAV is exact, but
  compressed formats were not tested.
- A seek drops up to 500 ms of decoded audio and decodes again on the owner
  thread, so seeking every frame would cost decode time.
- Voice virtualization (S02) remains open.
- Nobody has listened to the restart rewind in manual play.
