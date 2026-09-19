# Native audio service validation

`native/miniaudio_service.h` is the first native audio owner. It keeps one
miniaudio context and playback device, decodes bounded clips before publishing
them, and mixes fixed clip and voice slots in the device callback. Clip and
voice handles carry generations, so a stopped or shut-down voice cannot be
used accidentally. The callback performs no allocation or file IO.

## Evidence

The native Wicked gate and the sanitizer boundary harness exercise the service
through `native/miniaudio_probe.h`. The probe opens miniaudio's null backend,
decodes the generated 400-frame WAV, starts two voices, verifies non-zero
mixed samples, rejects a stale stop handle, keeps a looped voice alive, and
invalidates it during shutdown:

```text
miniaudio: frames=400 read=400 rate=8000 channels=1 backend=14
boundary harness: ozz=1 recast=1 audio=1 text=1 udp=1 menu_and_pose=1
sanitized boundary harness passed: no AddressSanitizer or UBSan finding
```

The null backend makes this check deterministic on headless machines. Device
loss/reopen, streamed clips, buses, spatial attachment, and gameplay event
ownership remain follow-up work under S01–S05.

