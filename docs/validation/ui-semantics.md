# UI semantic metadata

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I06 progress: semantic control metadata, reported separately from the
visual accessibility options.

## Design

- `src/runtime/ui_semantics.elisa` (`UiSemantics`) defines `Control`: role
  (button, slider, toggle, key binding, label), label, current value,
  zero-based index, list count and selected flag.
- The constructor rejects a count outside 1..64 and an index outside the list
  with `SemanticsError.Invalid`.
- `adjustable` is true only for sliders and toggles. `focusable` is false for
  labels. `position` is the one-based place a reader would announce.
- The course's `row_semantics` (`play.inc`) describes each of the nine menu
  rows: six key bindings, the text-size slider and the two toggles.

## Checks

- `test/ui_semantics.elisa` exits 0 and is in the `scripts/check.elisascript`
  unit-test loop. Negative control: expecting the wrong role name exits 14.
- The course self-test runs code 201: row roles, the selected flag, position
  9 of 9, the live value (`< 100% >`) and a refused out-of-range row.

## Gaps

- No platform accessibility bridge (VoiceOver, UIA, AT-SPI) consumes these
  controls.
- Only the pause menu is described; the HUD and legend are not.
- Reduced motion is not offered: the course camera does not move on its own.
