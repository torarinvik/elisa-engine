# Localized label layout

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`). This is I10 progress and follows
[`message-format.md`](message-format.md).

## Design

`src/ui/localized_label.elisa` (`UiLocalizedLabel`) lays out formatted message
text in a fixed label box:

- It splits the text into words at spaces and measures each one. CJK and
  fullwidth codepoints advance a full em and everything else half an em.
- It wraps the words with `UiText::wrap`.
- Each line sits at the reading edge. For right-to-left locales it is
  mirrored with `UiLocaleCatalogue::mirror_x`.
- Text that needs more lines than the box allows, or a word wider than the
  box, gives `fits = false` instead of being clipped silently.

## Checks

`test/ui_localized_label.elisa` exits 0. It covers:

- an English line of exact width;
- Arabic placed flush right;
- a Norwegian translation wrapping to two lines in its reserved width
  (`reserve_width`, 208 px);
- the same text rejected in a one-line box;
- a Japanese unbreakable word measured at full width;
- an overflowing narrow box;
- an overlong right-to-left line starting at the reading edge.

Negative control: dropping the RTL mirroring makes the test exit 3. The test
is wired into `scripts/check.elisascript`.

## Gaps

- The advance table is a stand-in. There is no font metrics, shaping,
  kerning or bidi reordering inside a line.
- It doesn't handle line breaking for CJK without spaces, message extraction
  or packaged locale data.
