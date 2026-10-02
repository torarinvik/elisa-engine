# UiSettingsView

`src/ui/settings_view.elisa` draws a `UiSettingsPage` onto a `UiWidgets::Surface`.

- Only rows in the list window become Slider widgets (id = row + 1), so a long page uses at most `rows` widgets. Window sizes outside 1..16 draw nothing.
- `click` hit-tests the surface and focuses the row it lands on. A miss leaves the page unchanged.
- Removed rows disappear from the next layout.

`test/ui_settings_view.elisa` (in the gate) covers window bounds, ids, values and positions, click and miss, scrolling, removal and bad window sizes. Negative control: ignoring the window start (always drawing from row 0) fails with code 5.
