# Relative mouse mode (mouse-look)

`Application::set_relative_mouse(enabled)` turns relative mouse mode on or off for the application window, through `elisa_application_v1_set_relative_mouse` (`SDL_SetWindowRelativeMouseMode`).
While it is on, the cursor is hidden and confined, and motion deltas keep arriving past the window edge. `ActionPointerAxes` turns those deltas into look axes.

Results:
- `Enabled` or `Disabled`;
- `Unsupported` when the platform refuses (the mode is then forced off);
- `NotRunning` before initialization or off the owner thread.

`Application::relative_mouse()` reports the live mode.

Native coverage in application-native-smoke:
- before initialization the request reports `NotRunning`;
- after initialization, an enable must either report `Enabled` with the mode live, or `Unsupported` with it off;
- disabling must report `Disabled`, with the mode off.

On macOS the mode engages: a control whose getter always returns 0 makes the smoke fail with 191, the Enabled-but-not-live check.
Not yet covered: the course switching into this mode for its camera, and releasing it while a menu is open.
