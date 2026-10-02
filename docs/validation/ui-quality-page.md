# UiQualityPage

`src/ui/quality_page.elisa` connects the generic `UiSettingsPage` to the persisted render quality profile (`Quality::Profile`, saved through `QualitySettings`).

- `page_of(profile)` builds nine rows: level, render scale (25–100 %, step 5), bloom, FXAA, TAA, AO, SSR, fog and shadow quality. It loads them from the profile. Saved values outside a row's range fall back to the row default and are flagged as recovered by the slider.
- `apply(page, base)` writes the rows back over `base`. Fields the page doesn't show (tonemap, upscaler, thresholds, biases) are kept. If the result would be invalid (TAA with FSR2), the whole edit is refused and `base` is returned.

`test/ui_quality_page.elisa` (in the gate) covers loading, an unchanged round trip, nudged edits, a save-blob round trip, refusing an invalid edit and recovering from a corrupt value. Negative control: removing the validity refusal fails with code 7.
