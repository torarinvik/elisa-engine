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

## Character course and relaunch

examples/character_course/devices.inc wires the settings into the course:

- **Play:** at startup `devices_start()` reapplies a saved `course-devices` record. It moves the window with `apply_display`. It calls `reopen_audio` before any clip is decoded, and if no output opens it falls back to the silent device. On quit, `devices_quit()` saves the display that now holds the window and keeps the saved output.
- **Self-test:** `devices_handoff()` (codes 260–261) saves the primary display and the default output.
- **Relaunch:** a second process runs `devices_relaunch_test()` (codes 262–265). It loads that record, reapplies it with source `Saved` and puts the window on the saved display. It reopens the mixer as `Chosen`, then removes the record.

Both smokes passed. A negative control made the handoff save no display. The relaunch then exited with status 6, which is 262 truncated to 8 bits.

Remaining gaps:
- There is no in-game menu for picking a display or an output.
- Only one display was available.
