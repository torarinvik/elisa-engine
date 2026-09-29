# Course language switching (I10, 2026-09-30)

The character course's access menu gains a Language row (row 10 of 13, before
the three pad rows). Left/Right cycles English, Norsk, Русский, العربية and 日本語,
each named in its own language; screen readers see it as an adjustable slider.

- `CourseText::load` pre-encodes every caption for all five locales from
  `text/course.loc`, and `CourseText::line(captions, locale, event)` picks one.
  Changing the language only changes which bytes the overlay gets. A caption
  already on screen is redrawn in the new language straight away.
- The language is saved with the other access settings as
  `course-access` version 2 (five fields). A version 1 save still loads,
  as English, so the upgrade doesn't reset anyone's settings. An unknown
  locale, or any version other than 1 or 2, is refused and defaults kept.
- The relaunch hand-off now saves Russian and checks that it comes back.

Codes: 180 (captions in all locales, invalid locale falls back to English,
row wraps both ways, the Japanese label), 181 (locale 5, -1 and version 3
refused), 182 (version 1 migrates as English), 201 (Language row semantics).
Evidence: `character-course-smoke` and `character-course-relaunch-smoke` pass.
Negative control: reading the locale field from version 1 saves makes the smoke fail with 182.
