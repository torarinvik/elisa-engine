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
below the public minimum, and injects a test-only failure after the null device
starts. The adapter shuts the device down before returning `DeviceUnavailable`;
the public API then reports `ApplicationUnavailable` for service access. The
caller closes the host, verifies the profile is closed, re-negotiates the same
provider, and successfully initializes the silent device on retry. Native fault
injection is enabled only for the application smoke build.

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

## Elisa-owned optional handle state

`AudioRuntime` now exposes `empty_clip()` and `empty_voice()` sentinels plus
`clip_is_empty()` and `voice_is_empty()` checks. Elisa state can represent an
optional clip or voice without depending on zeroed ABI data or exposing slot
and generation fields. The native application smoke rejects attempts to play
an empty clip or stop an empty voice with `InvalidHandle`.

Validation on 2026-09-21:

- `DEVELOPER_DIR="$(xcode-select -p)" python3 scripts/application_native_smoke.py` passed on macOS with SDL3, Metal, and Wicked. Both the normal audio/application smoke and injected startup-failure cleanup smoke passed.
- `python3 scripts/check_source_length.py` and `python3 scripts/check_module_hygiene.py` passed.

Device-loss recovery is now owner-thread driven. Miniaudio's device notification
callback only publishes an atomic recovery request; `RuntimeServices::pump`
consumes it and reopens the adapter on miniaudio's silent backend. The active
provider accessor reflects that route change. Recovery preserves decoded clips,
invalidates active voices, and permits a preserved clip to play again. If reopen
fails, the session marks Audio inactive and returns
`RuntimeServicesError.AudioRecoveryFailed` instead of claiming audio is live.
The native application smoke injects the actual stopped-notification callback,
then checks provider change, voice invalidation, clip retention, replay, and
shutdown cleanup.

Validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh /opt/homebrew/bin/python3 scripts/application_native_smoke.py` passed both normal and startup-failure cleanup runs on SDL3/Metal/Wicked, including the synthetic device-loss/reopen path.
- `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript` passed the portable suite, Godot 4.7.2 compatibility check, both proof suites (17/17 and 6/6), and the validation report.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/native_gate.elisascript native` passed on macOS 27.0 / Apple M5 against the pinned Wicked checkout; the report recorded `hardware_verification=verified`. This included both Metal-rendered probe passes, exact frame determinism, live-input rendering, frame-time budget, asset checks, the device-loss application smoke, and orderly shutdown.

See [`elisa-render-scene.md`](elisa-render-scene.md) for the Wicked pin and
renderer evidence.

Spatial source/listener submission, streaming decode, and live profile
advertisement remain open work. This recovery test uses a deterministic
synthetic device notification and a silent fallback; it does not measure
audible output on physical speakers or a headset.
