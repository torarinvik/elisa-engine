# Display enumeration

`ApplicationDisplays::list()` (src/runtime/application_displays.elisa) returns up to eight connected displays from the native host (native/application_displays.inc). Each display has its SDL id, desktop bounds, refresh rate in millihertz, content scale in permille, and a primary flag. `contains(displays, id)` lets a settings layer check whether a saved display is still connected before it applies the `DeviceChoice` fallback. Before the host starts, `list()` reports `ok = false` with no displays.

Validation: test/application_displays_probe.elisa runs in application-native-smoke with codes 202–206. It checks the not-running result, that at least one display is listed, that sizes, scale and ids are valid, that exactly one display is primary, and that an unknown id is not found. A negative control that clears the primary flag fails with 205. The smoke ran on one Mac display. Hot-unplugging a display has not been exercised with real hardware.
