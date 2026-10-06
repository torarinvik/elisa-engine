# Character Course audio teardown

`CourseSounds::stop` now aggregates decision-log, voice, stream, virtual-sound,
bus, clip-release and gain-restore results while continuing through every
cleanup step. Scene reset reports whether event voices stopped. A stale event
voice handle is treated as already silent; a failed stream stop remains a
reported error.

The hidden Character Course smoke injects a voice-stop and a stream-stop
failure. The first `CourseSounds::stop` reports failure, releases the other
resources, and retains both failed handles. A second call retries through the
owner and reaches zero active voices and streams. Failed event-voice stops also
keep their voice-pool entries and event timeline intact; failed clip releases
and virtual-sound removals retain their handles for retry. The sound set remains
live while any cleanup step fails, so an unsuccessful teardown cannot be
mistaken for a completed one. The source is split into a teardown include to keep
`sounds.elisa` below the 600-line source limit.

## Validation

On 2026-10-05, the production and test-probe C++ audio ABI variants passed
`clang++ -std=c++17 -fsyntax-only`. The SDL3/Metal
`character-course-smoke` passed with `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`
using Stage1 `7b27fa312c5af923f044f6ee0e5e1de4f811f595`; this forces miniaudio
onto its silent provider, and Wicked's SDL audio remains on its dummy driver.
The smoke includes the injected failure and retry case.

On 2026-10-06, the Character Course smoke passed on Stage1
`7b27fa312c5af923f044f6ee0e5e1de4f811f595` with
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`. The test injects one voice-stop and
one stream-stop failure. Its first `CourseSounds::stop` retains exactly one
active voice and one stream; retrying through the owner reaches zero of each.
Wicked SDL audio uses its dummy driver, so this check produces no audible
output. The captured run reported `voices=0 streams=0` in every teardown stress
iteration and passed `character-course-smoke`.

No separate proof file applies: this slice changes effectful teardown and adds
no pure state policy. The native failure injection covers voice-stop and
stream-stop failures; clip-release, virtual removal, bus reset, and persistence
failures do not yet have separate injected cases.
