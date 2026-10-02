# Screen-reader bridge

`ApplicationAccessibility::announce` (src/runtime/application_accessibility.elisa) speaks a control as "label, value, role, N of M". The native host (native/application_accessibility.inc) checks each part as UTF-8 of at most 256 bytes, composes the text, and on macOS posts it to the SDL window's `NSWindow` as an `NSAccessibilityAnnouncementRequestedNotification` with medium, low or high priority. VoiceOver reads these requests while it is running. Other platforms return `Unsupported`. Calls made before the host starts return `NotRunning`. An empty label, a position past the count, or malformed UTF-8 return `Invalid`, and the last posted text stays unchanged.

The character course announces the focused settings row each time the menu opens, the selection moves, or a value changes. It builds the text from the row's `UiSemantics` control. Device rows give only their name, because the display or output name lives in the legend text and not in the row metadata.

Validation:
- test/application_accessibility_probe.elisa runs in application-native-smoke with codes 177–179. It checks that a call before start is refused, that "Text size, 125%, slider, 8 of 16" is posted and read back byte for byte, and that malformed requests are refused and leave the posted text unchanged. A negative control that replaced " of " with "/" failed with 178.
- The course self-test (code 233) announces the text-size row (36 bytes) and the first device row (25 bytes) through the play loop's helper. It also checks that a row past the menu is refused.

Gaps:
- No check confirms that VoiceOver actually speaks the text. The smoke proves only that the request reaches AppKit with the window as its element.
- There is no accessibility element tree: the menu is not exposed as `NSAccessibility` children, so VoiceOver cannot browse it, only hear announcements.
- Windows (UI Automation) and Linux (AT-SPI) have no bridge.
