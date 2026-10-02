# UI focus navigation

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I02 progress, and it feeds I06.

## Design

- `src/ui/focus_nav.elisa` (`UiFocusNav`) registers up to 32 widgets by box
  and enabled flag.
- `step_dir` moves to the enabled widget whose centre lies strictly in the
  pressed direction and has the lowest cost: distance along the axis plus
  3 × distance across it. Aligned widgets beat nearer diagonal ones, and ties
  keep the earlier widget. With no focus, it takes the first enabled widget.
- `step_seq` is Tab and Shift-Tab in registration order. It wraps around and
  skips disabled widgets.
- Disabling the focused widget clears focus. Disabled and out-of-range widgets
  refuse focus.

## Checks

- `test/ui_focus_nav.elisa` exits 0 (codes 1–20) on a 3×2 grid with a
  disabled cell. It covers the initial focus, all four directions, edges with
  no target, skipping the disabled cell, Tab wrapping in both directions,
  focus clearing, a single widget and an empty set. It is in the gate's
  asset-test list.
- Negative control: dropping the cross-axis weight makes the test exit 9.

## Gaps

- Not yet connected to the menu renderer, the HUD or SDL3 input; there is no
  focus ring drawing and no proof harness. I02 stays open.
