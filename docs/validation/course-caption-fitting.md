# Course caption fitting and right-to-left anchoring

- `wrapped_caption` (play.inc) wraps each packaged caption with
  `UiLocaleData::wrap_utf8` to `caption_width` (label plus key columns) at the
  current text size before handing it to the overlay.
- `caption_anchor_x` hangs Arabic captions from the legend's right edge with
  Right alignment; other locales start at the left edge with Left alignment.
- Course test `captions_fit` measures all 30 captions (6 × 5 locales) at 150%
  through `measure_overlay_text_utf8`, pumping frames until measurements are ready:
  - 183: the widest wrapped caption is not inside the caption line.
  - 184: the captions did not load, or an anchor is wrong.
- Both course smokes pass. Control: measuring unwrapped lines fails with 183
  (the Russian landing caption is too long unwrapped).
- Open: glyph shaping (Arabic joining is left to the font renderer) and bidi
  reordering of mixed-direction text.
