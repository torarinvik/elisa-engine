# Course captions from packaged locale data (I10, 2026-09-29)

The character course's sound captions now come from
`examples/character_course/text/course.loc` (en, nb, ru, ar, ja), shipped as a
package resource, so the caption text changes without rebuilding gameplay code.

- `CourseText::load(path, locale)` reads the file through the asset reader,
  loads it with `UiLocaleData::load`, resolves each `MSG_CAPTION_*` key (with
  English fallback) and pre-encodes it with `UiLocaleData::encode`.
  The play loop loads English; the locale is a `load` argument.
- The overlay gains `RenderScene::set_overlay_text_utf8`, a
  length-delimited entry point (`elisa_render_scene_v1_set_text_utf8`) that
  rejects malformed UTF-8, NUL, overlongs, surrogates and text over 256 bytes.
- If the file is missing or malformed, `loaded` stays false and the built-in
  English captions are shown.
- `python3 scripts/locale_keys.py check --loc examples/character_course/text/course.loc examples/character_course/text.elisa examples/character_course/play.inc`
  reports no errors or warnings.

Self-test code 180 (in access_test.inc) checks that the English jump caption
matches the built-in text byte for byte, that the Russian caption is the
expected 24 UTF-8 bytes, that a missing file refuses, and that unknown events
map to the chime key. `test/ui_locale_data.elisa` codes 25–26 check the
encoder on Cyrillic and Japanese text.

Evidence: `character-course-smoke` passes (the smoke now stages `text/`).
Negative control: shortening the Russian jump caption in course.loc makes the
smoke fail with status 180.
