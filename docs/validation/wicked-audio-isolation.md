# Wicked audio isolation

S01 asks for one native audio-device owner, with Wicked's overlapping FAudio
playback path disabled or isolated. `wi::initializer` starts `wi::audio`
immediately, and Wicked's FAudio backend (`FAudio_platform_sdl3.c`) calls
`SDL_InitSubSystem(SDL_INIT_AUDIO)` and opens an SDL audio device stream.
Every `NativeApplication` therefore had a second real output device running
next to the miniaudio service.

## Change

- `NativeApplication::initialize` (`native/native_application.h`) sets
  `SDL_HINT_AUDIO_DRIVER` to `dummy` at override priority, right after SDL
  starts and before Wicked initializes. FAudio's mastering voice then opens on
  SDL's silent dummy driver. `wi::audio` calls keep working, but nothing
  reaches a real device. Real output belongs to the miniaudio service, which
  opens Core Audio directly rather than through SDL.
- After startup, `LifecycleTelemetry::wicked_audio_isolated` records that SDL
  audio is initialized and that its current driver is `dummy`.
- `probe_window_lifecycle` (`native/window_lifecycle_probe.h`) fails with
  "Wicked FAudio is isolated on SDL's dummy audio driver" if the flag is false.

The Wicked checkout was not changed. It has another session's uncommitted
FAudio edits, and the isolation does not need a Wicked change.

## Checks and commands

```
elisascript scripts/wicked_probe.elisascript build-gate && elisascript scripts/wicked_probe.elisascript frame
elisascript scripts/native_gate.elisascript native
PYTHONPATH=scripts python3.14 scripts/application_native_smoke.py --only character-course-smoke,character-course-relaunch-smoke
elisascript scripts/check.elisascript
```

On 2026-09-28 the native gate exited 0 with every stage passing, including the
Wicked probe's `build-gate` and `frame` stages, which run the isolation check
(`build/native-gate.json`). The course and relaunch smokes exited 0, and `check`
exited 0 with `failed: 0`.

## Negative control

With the hint line removed, the probe's frame stage exited 1 with
`wicked probe failed: Wicked FAudio is isolated on SDL's dummy audio driver`.
The file was restored, and `cmp` confirmed it matches the verified copy.

## Gaps

- SDL's dummy driver still runs a mixing thread for Wicked's FAudio. It is
  silent, but it still uses a little CPU. Removing it needs a Wicked
  initializer option that skips `wi::audio`.
- Nobody has checked by ear that only one output plays.
- The physical device-unplug case for S01 still needs hardware.
