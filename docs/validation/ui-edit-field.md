# Editable text field policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I05 progress. It is the editing model only; no SDL3 text event, clipboard
or on-screen field is connected.

## Design

`src/ui/edit_field.elisa` (`UiEditField`) is a single-line field of up to 32
Unicode code points with a caret and selection anchor.

- Insert replaces a selection; invalid code points (zero, surrogates, above
  U+10FFFF) and a leading combining mark are refused; a full field refuses more.
- Backspace, forward delete and caret movement work on graphemes: a base plus any
  following combining marks (U+0300–036F), variation selectors (U+FE00–FE0F) or
  zero-width joiners. This is a documented subset of full grapheme clustering.
- Composition keeps up to 8 preedit code points apart from the text; cancel leaves
  the text untouched, commit inserts them (refused if they would not fit).
- One level of undo restores the text and caret before the last edit.
- A password field displays bullets and refuses copy.

## Checks

- `test/ui_edit_field.elisa` exits 0 (codes 1–18).
- Negative control: making backspace ignore combining marks makes it exit 8.
- Source-length check passes.

## Gaps

- No SDL3 text/IME events, no clipboard, no multi-level undo, no full Unicode
  grapheme rules (emoji sequences, Hangul jamo, regional indicators), no DPI
  test, and no check that gameplay input is withheld while focused, so I05's done
  condition is not met. Full gate still blocked at `world-test`.
