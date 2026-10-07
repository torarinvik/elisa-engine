# Native audio service validation

`native/miniaudio_service.h` is the first native audio owner. It keeps one
miniaudio context and playback device, decodes bounded clips before publishing
them, and mixes fixed clip and voice slots in the device callback. It provides
music, SFX, and UI buses with bounded budgets, gain, and priority-based voice
stealing. Clip and voice handles carry generations, so a stopped or shut-down
voice cannot be used accidentally. It also exposes explicit listener state,
distance attenuation for spatialized sources, and a device reopen operation;
the callback performs no allocation or file IO.

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
reopen, bounded bus/voice policy, and listener-relative distance attenuation
are covered here. Streamed clips, gain ramping, occlusion, Doppler, entity
attachment, and gameplay event ownership remain follow-up work under S02–S05.

## Device lifecycle (S01, 2026-10-02)

Every device close goes through one `Service::close_device`, which stops
notifications, calls `ma_device_uninit` (it joins the callback thread) and
lowers a process-wide open-device count. Test hooks in
`native/miniaudio_service_test_hooks.inc` (compiled only with
`ELISA_AUDIO_TEST_PROBE`) inject device-open failures, simulate device loss and
read the per-service data-callback count. `native/miniaudio_lifecycle_harness.cpp`
runs on live null devices under ASan/UBSan and TSan from
`scripts/run_boundary_sanitized.py`. It checks that:

- a failed open leaves no device, no callback and refuses clips, and a retry opens;
- a second open is refused while one device is live;
- loss raises one recovery request, and reopen keeps exactly one device, ends voices and keeps clips;
- a failed default reopen falls back to the silent route; if both routes fail, nothing is open, no callbacks arrive and playback is refused until a later open succeeds;
- shutdown, destruction and 20 open/reopen/shutdown cycles (with injected failures) leave zero devices and no further callbacks.

Removing the injection hook makes nine checks fail (negative control). No
physical output device was unplugged; loss is simulated with miniaudio's
`stopped` notification.

## Workload snapshots (2026-10-07)

`AudioRuntime::workload_snapshot` and the session-checked
`RuntimeServices::audio_workload_snapshot` expose live resource counts,
allocated PCM/ring capacities and cumulative callback/contention counts.
Decoder/device internals and allocator overhead are excluded. Lifecycle and
stream harnesses verify buffer retention on recovery/cancellation and release
on shutdown under ASan/UBSan and TSan. The Character Course uses actual sounds
for route and binding-reload measurements; see
[workload evidence](course-audio-workload.md).
