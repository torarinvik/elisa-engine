# Locale key extraction (I10, 2026-09-29)

`scripts/locale_keys.py` treats `const MSG_NAME: i64 = N` declarations in Elisa
source as the message-key registry (ids 0..31, UiLocale::KEYS) and checks a
`.loc` file in the UiLocaleData syntax against it.

- `extract SRC...` lists declared keys and whether code uses them.
- `check --loc FILE SRC...` fails on undeclared uses, id clashes, ids out of
  range, keys without English text, orphan text, unknown locales or plural
  categories, duplicates, over-long text, invalid UTF-8, and placeholder sets
  that differ from English. Untranslated keys and unused keys are warnings,
  because runtime fallback to English covers them.
- `template --loc FILE --locale nb SRC...` prints the missing lines with the
  English text as a comment, ready for a translator.

`--self-test` runs in the gate after the scene-file self-test. Negative
control: disabling the placeholder comparison makes the self-test exit 1.
