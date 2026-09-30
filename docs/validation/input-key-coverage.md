# Keyboard code coverage

Validated on 2026-09-30 on macOS with SDL3 from Homebrew. This is I01
progress on "full key coverage".

The portable keyboard table grows from 29 keys to 83. New codes 2030–2083
cover:

- the remaining letters and digits 0 and 4–9;
- F1–F12;
- Enter, Backspace, Delete, Insert, Home, End, PageUp and PageDown;
- the left and right Alt and Super keys, and Caps Lock;
- the US punctuation keys (- = [ ] \ ; ' ` , . /).

Existing codes are unchanged. The mapping uses SDL keycodes, as before, so
punctuation follows the active keyboard layout, not the physical position.

## Checks

`scripts/test_input_codes.py` now runs in `scripts/native_unit_tests.py`,
which the gate calls. It checks:

- every `KEY_`/`GAMEPAD_` constant has the same value in
  `src/runtime/application_input.elisa` and
  `native/application_input_codes.h`, and no value repeats;
- every `KeyboardKeyCode` member has exactly one `keyboard_key_code` arm;
- the native SDL switch returns each `KEY_` exactly once.

It then compiles and runs `test/application_gamepad_codes.cpp`, which no gate
ran before. That test now also checks F1, F12, Enter, left Super, Slash and 0,
and that Print Screen stays unmapped. The script checks its own detection by
changing one Elisa value and dropping one switch arm.

Negative control: mapping SDLK_F5 to KEY_F6 makes the script fail.
`character-course-smoke` still passes with the larger native switch, and the
recording test binds `KeyboardKeyCode.Slash`.

## Gaps

- There are no numpad, media or international keys.
- Scancode (physical position) bindings are not offered.
- No physical keyboard session was recorded.
