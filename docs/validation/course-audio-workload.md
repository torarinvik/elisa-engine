# Character Course audio workload — 2026-10-07

## Scope and instrumentation

`AudioRuntime::workload_snapshot` reports the native service's live clips,
voices and reserved streams, actual PCM and ring vector capacities in bytes,
and cumulative data/contended callback counts. Resource counters use one
service lock; callback counters are independent atomics. The owner requests
snapshots; the callback performs no new work or allocation.

These bytes measure owned sample buffers. They exclude decoder/device
internals, allocator overhead, Wicked audio and the rest of the process.
Both fixed rings remain allocated after stream cancellation and are freed
at service shutdown. A reserved muted stream still consumes its ring.

The existing live-input pilot samples actual course sounds during play,
pause/menu and restart. It retains peak counts/capacities and the largest
observed music/victory underrun counts across stream resets. Before teardown
it requires actual loaded sounds and callbacks, two streams, at most 16 clips
and 32 voices, PCM <=8 MiB, rings <=1 MiB, and zero stream underruns. After
`CourseSounds::stop`, it requires zero clips/voices/streams and zero PCM;
fixed ring storage must remain unchanged until service shutdown.

The course self-test samples its existing `events.sfx` → `events_alt.sfx` →
`events.sfx` reload. Clip/stream counts and PCM/ring capacities must remain
stable; explicit stop must release resources. This tests event binding
metadata reload, not replacement of decoded audio files.

## Discovery and targeted fix

The first measured live route failed status 228: both streams accumulated
38,880 underrun frames (0.81 seconds each at 48 kHz). PCM stayed at 1,105,920
bytes and rings at 192,000 bytes; peak counts were 8 clips, 3 voices and 2
streams. Stop released all resources and PCM. Two contended callbacks were
also recorded separately: those callbacks emit silence rather than wait for
the service mutex and are not stream-underrun events.

The synchronous screenshot encoder was measured independently at the pilot's
2560×1440 packed Metal pixel format with `clang++ -O0`: 1,109,058–1,167,825 µs
for three samples. This exceeds the existing 500 ms stream ring budget.
`rgba_png.h` now uses a constexpr CRC table, a preallocated block with indexed
writes, and overflow-safe Adler reductions every 5552 bytes. The same three
samples took 128,153–130,953 µs. This is a local measurement, not a portable
timing guarantee. The ring budget and zero-underrun assertion are unchanged.

The PNG self-test verifies channel conversion, padded rows, bounds, CRC,
exact and multiple stored-block boundaries, and a full 2560×1440 all-255
image stressing Adler sums. Largest encoder allocation stays <=65,535 bytes.
It passed after the optimization.

Pre-fix records are retained in ignored `build/validation/course-audio-before`
JSON/log files and `png-timing-before.log`; post-fix encoder samples are in
`png-timing-after.log`. The timed live route passed (199.301s): win/fall captures took 118,037 and
124,391 µs; peak refill gaps were 119,412 and 125,786 µs. It recorded zero
stream underruns and zero contended callbacks, with peak 8 clips, 4 voices
and 2 streams. The clean rerun without timing traces also passed (194.134s):
sampled peak 8 clips, 3 voices and 2 streams, PCM 1,105,920 bytes, rings
192,000 bytes, 1101 observed callbacks, zero underruns and zero contended
callbacks. Explicit stop released all clips/voices/streams and PCM; rings
remained 192,000 bytes until service shutdown.

## Recovery and limits

The lifecycle harness checks measured clip/voice buffers across device loss,
reopen and shutdown under ASan/UBSan and TSan; both passed. The stream harness
also checks measured reserved streams/rings across existing recovery,
cancellation, slot reuse and shutdown. Fresh ASan/UBSan and TSan stream runs passed with zero underruns and
zero contended callbacks. The course binding-reload self-test passed
(186.636s), with unchanged 1,105,920-byte PCM and 192,000-byte rings at all
three reload stages.

Output is forced unavailable in the course smokes: miniaudio's live null
device runs its callback while actual audio assets are decoded and mixed.
This establishes automated resource/stream behavior. Physical listening,
unplug/reconnect, other output devices and optimized packaged performance
remain external acceptance work. Contended callback silence is measured
separately and must not be described as verified glitch-free playback.

## Reproduce

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 \
/opt/homebrew/bin/python3.14 scripts/application_native_smoke.py --only character-course-live-input-smoke,character-course-smoke
python3 scripts/rgba_png_self_test.py
```

Structured smoke reports/logs are in `build/native-smoke/`. Sanitizer
stream/lifecycle harnesses use `run_stream_harness` in
`scripts/run_boundary_sanitized.py` with `address,undefined` and `thread`.
