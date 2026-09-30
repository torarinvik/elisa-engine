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

## Character course mouse-look

`examples/character_course/look.elisa` (`CourseLook`) turns the course camera with relative pointer motion. The play loop asks for relative mode only while the course is playing with the menu closed, and asks again only when that state changes. It releases the mode on exit. While playing, pointer events are drained into the camera yaw. Motion turns the camera only when `Application::relative_mouse()` reports the pointer is held. Movement keys are rotated by the same yaw, so forward always moves away from the camera. At zero yaw, the camera and movement axes match the old fixed camera.

The self-test `look_test` (codes 240–247) checks five things:

- zero-yaw identity;
- a quarter turn maps forward to −z;
- the camera keeps its bearing to the movement axis;
- a long drag wraps into [−π, π];
- relative mode on the live window engages and releases (or is refused cleanly), and a free pointer never turns the camera.

`python3 scripts/application_native_smoke.py --only character-course-smoke` passed. A negative control that flips the rotation's z sign fails the smoke with status 241. No physical mouse drag was recorded.
