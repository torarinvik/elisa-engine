# Saved device settings

`DeviceSettings` (src/runtime/device_settings.elisa) saves the chosen display and audio output in a versioned user-data payload. The display is remembered by its desktop bounds, because SDL display ids are not stable across launches. The output is remembered by its name. At startup, `apply(saved, rate, channels)`:

- lists displays and resolves the saved one through `DeviceChoice` (saved, then primary, then first);
- centres the window on that display (`ApplicationDisplays::move_window_to`);
- opens the mixer through `AudioDevices::open_preferred`.

A fallback never rewrites the record, so reconnecting a device restores the choice on the next launch. `load` reports `NotFound`, `Invalid` (wrong magic, version, length or flag) or `StorageFailure` instead of applying damaged data.

Validation: test/device_settings_probe.elisa runs in application-native-smoke with codes 215–226. It checks four things:

- A missing key reports `NotFound`.
- The primary display and the default output round-trip through the saved file and reapply. The window is on the saved display and the output is opened as `Chosen`.
- With both devices gone, `apply` falls back to the primary display and the default output.
- A record with the wrong magic is rejected.

A negative control that ignores the saved display fails with 219. Gaps:

- Save and reapply happen within one process. There is no second-process relaunch run yet.
- Only one display was connected, so moving between displays was not observed.
- Nothing calls `apply` from the character course yet.
