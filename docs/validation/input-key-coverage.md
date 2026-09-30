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

## Numpad and lock keys (2026-09-30)

Codes 2084–2103 add the following keys to both tables and to the SDL switch:
- the numpad digits 0–9;
- numpad divide, multiply, minus, plus, Enter and period;
- Num Lock, Scroll Lock, Pause and the Menu (application) key.

Numpad Enter stays distinct from the main Enter key, so a game can bind them separately.
PrintScreen is still left unmapped, so the OS keeps it.

scripts/test_input_codes.py checks parity, enum-arm coverage and switch coverage. The C++ test now also checks KP_0, KP_ENTER (and that it is not ENTER) and MENU.
Negative control: mapping numpad Enter to the main Enter code makes the script fail. character-course-smoke still passes.
