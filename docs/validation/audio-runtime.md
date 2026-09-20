# Audio runtime adapter

`AudioRuntime` is an opt-in Elisa service backed by miniaudio. It is available
to a game after the game declares Audio fallback during application startup.
The public module keeps its clip and voice handles opaque; the native ABI owns
miniaudio devices, decoders, and mixer state.

The adapter currently supports silent and default playback devices, WAV file
decoding, bounded generation-checked clip and voice handles, looping playback,
three gain buses, voice stop, and application-ordered shutdown. The silent
device supports deterministic tests and fallback when no hardware output is
available. The default device reports an explicit device-unavailable error if
initialization fails. Audio calls require the initialized application's owner
thread. The callback mixes fixed voice slots without allocating; decoded clips
are published only after a complete bounded read.

The public API names its accepted sample-rate range (8 kHz–192 kHz) and channel
count (one or two); Elisa and the C ABI both reject invalid configurations.

The application native smoke creates a short 8 kHz mono WAV, initializes the
silent device, decodes the clip, plays and stops one voice, and checks that
application shutdown closes the service. Stale clip/voice generations are
preserved across service restarts. The standalone Wicked probe continues to
run the miniaudio decoder, null-device, and mixer checks through the shared
implementation translation unit.

The startup smoke also negotiates `MiniaudioSilent`, rejects a sample rate
below the public minimum, shuts down the application after that adapter error,
checks that the host profile is closed, then renegotiates and successfully
initializes the silent device.

Validation on 2026-09-20: the full `DEVELOPER_DIR=/Library/Developer/CommandLineTools
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript
scripts/wicked_probe.elisascript` run passed both Metal-rendered probe passes,
exact frame determinism, miniaudio decoder/null-device/service checks, the
generated-WAV Elisa application smoke, and orderly shutdown. `scripts/check.elisascript`
also passed the portable suite, Godot 4.7.2 compatibility probe, both proof
suites (23 obligations proved, 23 certificates replayed), and validation report.
The source-length, module-hygiene, and `git diff --check` gates passed.

Spatial source/listener submission, streaming decode, device recovery, and live
profile advertisement remain open work. The current service does not claim
spatialization or uninterrupted recovery after device loss.
