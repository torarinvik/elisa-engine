# Course pause-menu gamepad rows

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I01/I03 progress. It follows
[`course-pad-rebinding.md`](course-pad-rebinding.md), which listed the missing
in-game editor as a gap.

## Design

- The pause menu has 12 rows: six key slots, the three accessibility
  settings, then three gamepad rows (`Pad jump`, `Pad crouch`,
  `Pad restart`). `CourseControls::PAD_FIRST` marks the first gamepad row.
- Left/Right (arrow, D-pad or pointer click) on a gamepad row returns the
  existing `Adjusted` result. The play loop then calls `CoursePad::cycle`,
  which steps the slot through the six-button pool, wrapping. A button held
  by another slot swaps, so the map stays complete and duplicate-free.
- Typing a keyboard key on a gamepad row is ignored: it cannot assign a
  keyboard key.
- Closing the menu rebuilds the input map from the new pad map and saves it
  with the keymap and settings.
- Gamepad rows carry UI semantics as adjustable controls whose value is the
  button name.

## Checks

- Self-test code 216 checks cycling forward, back and wrapping with a swap,
  and that the map stays valid. Code 217 checks that Right on a gamepad row is
  `Adjusted` and a typed key is `Ignored`. Code 201 now also describes a gamepad
  row.
- The `character-course-smoke` native smoke passed (the run moved on to the relaunch smoke with no failure);
  the relaunch smoke result is recorded in the follow-up commit.

## Gaps

- No physical controller was used. Rebinding by pressing the target button,
  rather than cycling, is not implemented.
