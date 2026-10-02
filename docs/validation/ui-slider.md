# UI slider model

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I07 progress.

## Design

- `src/ui/slider.elisa` (`UiSlider`) is a bounded integer slider: range, step,
  default and current value. Every reported value lies in the range and on the
  step grid; the top of the range counts as a grid point, so a range that is
  not a multiple of the step still reaches its end.
- `set` clamps and snaps to the nearest grid point; `nudge` takes one
  keyboard/D-pad step (only -1 or +1) and stops at the ends.
- `load` treats a saved value outside the range as untrusted: the default is
  used and `recovered` is set, so a settings screen can show a validation
  state. The next `set` clears the flag.
- Invalid configurations (empty range, non-positive step, default outside the
  range) are marked invalid and ignore input.

## Checks

- `test/ui_slider.elisa` (in the gate) exits 0, codes 1–11: snapping and
  clamping, nudges at the ends, a non-multiple range, saved-value recovery,
  invalid configurations.
- Negative control: trusting every saved value (`bad` always false) makes the
  test exit 8.

## Gaps

- No drawing or pointer dragging; no settings screen uses it yet. Typed
  bindings, trees and inspectors remain, so I07 stays open.
