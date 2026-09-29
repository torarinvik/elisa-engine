# UI contrast checking

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is I06 progress. It is a colour-pair check only; no theme is loaded or
applied to the sample's widgets.

## Design

`src/ui/contrast.elisa` (`UiContrast`) computes WCAG-style relative luminance
in integers. Each 8-bit channel is linearised by interpolating a 17-knot table
of the sRGB curve, so the result is approximate (a few percent off in dark
tones, exact for black and white). `ratio` is in hundredths (2100 = 21:1);
`passes_text` needs 4.5:1, `passes_large` 3:1 and `passes_enhanced` 7:1.
Out-of-range channels give ratio 0 and never pass.

## Checks

- `test/ui_contrast.elisa` exits 0 (codes 1–8): black/white 21:1, identical
  colours 1:1, grey #767676 near 4.5:1, light grey failing, dark grey passing
  enhanced, yellow on white failing, luminance ordering, invalid colours.
- Negative control: lowering the large-text threshold to 1:1 makes it exit 4.
- Source-length check passes.

## Gaps

- The result is approximate, so a borderline theme should be confirmed with an
  exact tool. No theme, scalable-text, caption or reduced-motion setting is
  added, and no platform accessibility bridge is involved, so I06's done
  condition is not met. Full gate still blocked at `world-test`.
