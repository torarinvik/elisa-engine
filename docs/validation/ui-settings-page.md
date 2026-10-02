# UiSettingsPage

`src/ui/settings_page.elisa` builds a settings page from the reusable widget
models: rows are a `UiListModel` (D-pad focus order, focus repair), each row is
a `UiSlider`, and each row's edits reach its setting field through a
generation-checked `UiBinding`. `test/ui_settings_page.elisa` builds a game page
(volume, render scale, sensitivity) and an editor page (grid snap, autosave)
from the same type.

Checked (codes 1-9): invalid and duplicate rows are refused; D-pad focus stops
at the ends; nudges move by the row's step and the fired edit names the bound
field; pages are independent; out-of-range saves fall back to the default and
are flagged; removing the focused row moves focus and makes the row's old
callback handle resolve to no field, even after the row is re-added into the
same binding slot.

Negative control: `remove_row` that keeps the binding exits 8.

Not yet: drawing, text labels, and wiring to `QualitySettings`/audio settings
persistence.
