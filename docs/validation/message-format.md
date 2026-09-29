# Message formatting

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is I10 progress, following the locale catalogue work.

## Design

`src/ui/message_format.elisa` (`UiMessageFormat`) formats translated
templates held as bounded codepoint buffers (64 codepoints):

- `{0}`..`{3}` are replaced by decimal integer arguments, in any order, so a
  translation can reorder them. Negative numbers get a leading `-`.
- An unclosed, out-of-range or stray-brace placeholder gives
  `BadPlaceholder`. Output that would exceed the buffer gives `Overflow`
  instead of being silently truncated.
- `Plural` holds one template per CLDR category. `format_plural` picks the
  locale's category from `UiLocale::plural`, falls back to OTHER when that
  form is not authored, and substitutes the count as `{0}`.

## Checks

- `test/ui_message_format.elisa` exits 0. It covers substitution,
  reordering, negative numbers, three malformed templates, overflow,
  Russian one/few forms (21, 3), fallback to OTHER (RU 5, JA 1), English
  one, an empty plural, and rejecting an invalid category.
- Negative control: ignoring whether a form is authored makes the test
  exit 9.
- Wired into `scripts/check.elisascript`.

## Gaps

- No string extraction tool, packaged locale data or real layout exercise
  yet. Templates are built from codepoints in code, not loaded from assets.
- Arguments are integers only; there is no text or date argument.
