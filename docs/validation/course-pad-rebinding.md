# Course gamepad rebinding

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I01/I03/I06 progress. It follows [`course-controls.md`](course-controls.md),
which listed gamepad rebinding as a gap.

## Design

- `examples/character_course/pad.elisa` (`CoursePad`) holds the pad buttons
  for jump, crouch and restart. They come from a pool of six: the four face
  buttons and both bumpers. The D-pad, left stick, Start and Back stay fixed
  so a pad player can always move, pause and reopen the menu.
- `assign` mirrors the keyboard rule: a button held by another slot moves to
  the slot's old button, so the map stays complete and duplicate-free.
  Buttons outside the pool give `Reserved`; a bad slot gives `InvalidSlot`.
- The map is stored as a versioned `course-pad` user-data record. A wrong
  version, a duplicate button, a button outside the pool or a short record is
  rejected and the defaults apply.
- `CourseControls::bind` now takes the pad map, so the fixed
  South/East/West bindings are gone and the rebound buttons drive the
  actions. The course loads the record at start.

## Checks

- Course self-test codes 202–215 cover defaults, assign, swap, reserved,
  invalid slot, unchanged, live input (the old button no longer jumps, the
  new one does), a saved round trip, removal, and four rejected records.
- `scripts/application_native_smoke.py --only
  character-course-smoke,character-course-relaunch-smoke` and the
  source-length check are the gate for this slice.

## Gaps

- The pause menu has no pad rows yet, so the record can be loaded but not
  edited in game.
- Tests use synthetic events; no physical controller was used.
