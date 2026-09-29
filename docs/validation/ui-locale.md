# Localization policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`) and the `elisa-engine-proof` prover.

This is I10 progress.

## Design

- `src/ui/locale.elisa` (`UiLocale`) is integer policy with stable codes for
  locales (EN, NB, RU, AR, JA) and plural categories (zero, one, two, few,
  many, other).
  - `plural` follows CLDR-style rules: English and Norwegian distinguish 1;
    Russian uses the one/few/many rule with the 11-14 exception; Arabic uses
    zero, one, two, few, many; Japanese has no plurals. Negative counts are
    `other`.
  - `fallback` gives the next locale to try; every chain ends at EN.
  - `expansion_permille` is the text growth to plan for (up to 1500), and
    `is_rtl` marks right-to-left locales.
- `src/ui/locale_catalogue.elisa` (`UiLocaleCatalogue`) holds:
  - a catalogue of translated keys (32 per locale) with `translate`,
    `has_key` and `resolve`, which walks the fallback chain and reports
    `missing` only when not even EN has the key;
  - live switching (`switch_to`), which accepts only a known locale and bumps
    a generation so cached text and layout rebuild;
  - `mirror_x` for right-to-left layout and `reserve_width` for long text.

## Proof

`proof/ui_locale.elisa` proves that any locale and count give a known plural
category, that fallback names a valid locale, and that the expansion stays in
1000..1500 (60/60 replayed).

## Checks

- `test/ui_locale.elisa` exits 0 (codes 1-16): the four plural rule sets,
  fallback and missing-key reporting, unknown locales, invalid keys, live
  switching, mirroring and reserved width.
- Negative control: changing Russian's "one" unit rule fails code 2.
- Wired into `scripts/check.elisascript`.

## Gaps

- Keys map to integers only. There is no message text, `{name}` formatting,
  extraction tool, packaged locale data file or number/date formatting, and
  no font or shaping for Arabic, so I10 stays open.
- `mirror_x` is not proven: the prover fails on the three-term subtraction
  `container - width - x` even with the matching requires, so it sits outside
  the proved module and keeps its runtime contracts. This is a candidate
  prover hole.
- A long-text and right-to-left layout exercise in a real UI is not run.
