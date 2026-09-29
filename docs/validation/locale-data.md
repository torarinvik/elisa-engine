# Packaged locale data

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is I10 progress: "packaged locale data without rebuilding
gameplay code" is part of the item's done criterion.

## Design

`src/ui/locale_data.elisa` (`UiLocaleData`) loads translations from a text
asset (up to 4 KiB) instead of from code.

- **Format.** Each line is `<locale> <key> <text>`.
  - The locale is a two-letter code: en, nb, ru, ar or ja.
  - The key is a decimal message key below `UiLocale::KEYS`.
  - Exactly one space follows the key, and the text runs to the end of the
    line.
  - `#` lines and blank lines are skipped, and CRLF line endings are
    accepted.
- **UTF-8.** Text is decoded into codepoints for `UiMessageFormat::Text`.
  Overlong forms, surrogates, bad continuation bytes and cut-off sequences
  are rejected.
- **Loading.** A pack holds up to 48 entries, and loading is all or nothing.
  The first bad line gives one of `UnknownLocale`, `BadKey`, `Duplicate`,
  `BadUtf8`, `TooLong`, `TooMany`, `Syntax` or `Oversize`, with its 1-based
  line number, and the pack is left empty.
- **Lookup.** `text(pack, locale, key)` resolves through the
  `UiLocaleCatalogue` fallback chain. A key missing in one locale shows the
  English text and is flagged `fell_back`. A key no locale has is reported
  `missing`, with empty text.
- **Formatting.** Loaded templates go straight into
  `UiMessageFormat::format`.

## Checks

`test/ui_locale_data.elisa` exits 0 and is in the `check.elisascript` asset
list. It loads an eight-line pack in five locales and checks:

- ASCII text, Norwegian "å", Cyrillic, Arabic with a CRLF ending (RTL), and
  katakana, each with exact codepoints;
- fallback to English, and a missing key;
- formatting `{0}` in a packaged Japanese template;
- each failure status, with its line number and the pack emptied.

Negative controls on a copied tree:

- disabling the duplicate check exits 13;
- accepting bad continuation bytes exits 14.

The source-length check passes.

## Gaps

- Plural forms are not authored in the asset yet.
- There is no extraction tool that lists the keys used by code.
- The course does not load a `.loc` file yet.
- There are no real font metrics or shaping, which Arabic needs.

## Plural forms (2026-09-29)

A key may carry a plural category: `<loc> <key>:<zero|one|two|few|many|other> <text>`;
a bare key is `other`. `plural_text(pack, locale, key, count)` resolves the
locale's CLDR category through `UiLocale::plural` and falls back to the `other`
form, and then to the English fallback locale. Codes 17–24 cover English
one/other, Russian one/few/many (1, 3, 11, 21), Norwegian falling back to
English, an unknown category word (Syntax) and a repeated plural line
(Duplicate, line 2). Negative control: pinning the count to 5 gives 17.
