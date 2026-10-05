# Character Course audio teardown

`CourseSounds::stop` now aggregates decision-log, voice, stream, virtual-sound,
bus, clip-release and gain-restore results while continuing through every
cleanup step. Scene reset reports whether event voices stopped. A stale event
voice handle is treated as already silent; a failed stream stop remains a
reported error.

The hidden Character Course smoke injects one stream-stop failure. It verifies
that teardown reports failure, releases the other voices and stream, leaves
exactly the failed stream active, then successfully retries that retained
handle and reaches zero active voices and streams. The source is split into a
teardown include to keep `sounds.elisa` below the 600-line source limit.

## Validation

On 2026-10-05, the production and test-probe C++ audio ABI variants passed
`clang++ -std=c++17 -fsyntax-only`. The SDL3/Metal
`character-course-smoke` passed with `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`
using Stage1 `7b27fa312c5af923f044f6ee0e5e1de4f811f595`; this forces miniaudio
onto its silent provider, and Wicked's SDL audio remains on its dummy driver.
The smoke includes the injected failure and retry case.

No separate proof file applies: this slice changes effectful teardown and adds
no pure state policy. The native failure injection covers stream-stop failure;
voice-stop, clip-release, virtual removal, bus reset, and persistence failures
do not yet have separate injected cases.
