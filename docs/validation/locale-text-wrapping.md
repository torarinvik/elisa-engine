# Locale text wrapping and UTF-8 measurement

- `UiLocaleData::wrap_utf8(text, em, box_w)` turns spaces into newlines so UTF-8
  text stays within a box, estimating CJK/fullwidth glyphs at 1 em and others at
  3/5 em. An overlong single word stays whole.
- `RenderScene::measure_overlay_text_utf8(bytes&, length, font_size)` measures
  UTF-8 bytes through the native `elisa_render_scene_v1_measure_text_utf8`,
  refusing invalid UTF-8, so callers can confirm the estimate with real metrics.
- `test/ui_locale_data.elisa` codes 27–28: a Russian plural breaks at its space in
  a 40 px box, stays whole in a 200 px box, and a Japanese word stays whole.
  Control: widening the narrow box to 400 px gives 27.
- Not yet done: measured caption fitting in the course, right-to-left anchoring,
  and shaping.
