# Native audio service validation

`native/miniaudio_service.h` is the first native audio owner. It keeps one
miniaudio context and playback device, decodes bounded clips before publishing
them, and mixes fixed clip and voice slots in the device callback. It provides
music, SFX, and UI buses with bounded budgets, gain, and priority-based voice
stealing. Clip and voice handles carry generations, so a stopped or shut-down
voice cannot be used accidentally. It also exposes explicit listener state and
a device reopen operation; the callback performs no allocation or file IO.

## Evidence

The native Wicked gate and the sanitizer boundary harness exercise the service
through `native/miniaudio_probe.h`. The probe opens miniaudio's null backend,
decodes the generated 400-frame WAV, starts two voices, verifies non-zero
mixed samples, rejects a stale stop handle, keeps a looped voice alive, and
invalidates it during shutdown. It also rejects invalid initialization, stores
listener state, reopens the null device, and replays a decoded clip:

```text
miniaudio: frames=400 read=400 rate=8000 channels=1 backend=14
boundary harness: ozz=1 recast=1 audio=1 text=1 udp=1 menu_and_pose=1
sanitized boundary harness passed: no AddressSanitizer or UBSan finding
```

The null backend makes this check deterministic on headless machines. Device
reopen and bounded bus/voice policy are covered here. Streamed clips, gain
ramping, spatial attachment, and gameplay event ownership remain follow-up
work under S02–S05.
