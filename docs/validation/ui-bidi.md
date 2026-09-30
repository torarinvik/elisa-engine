# Bidirectional display order

`src/ui/bidi.elisa` (`UiBidi`) puts one line of `UiMessageFormat::Text` into
left-to-right display order. It is a reduced form of UAX #9:

- Hebrew, Arabic and related blocks are strong right-to-left.
- ASCII letters and anything else that is not neutral are strong
  left-to-right.
- ASCII digits follow the last strong letter (W2/W7).
- A neutral between two sides that agree joins them. Otherwise it takes
  the paragraph level (N1/N2).
- Levels come from I1/I2.
- Runs are reversed from level 2 down (L2), and brackets are mirrored at
  odd levels (L4).

`test/ui_bidi.elisa` (in the gate's unit-test list) uses Hebrew letters and
checks nine cases:

- a Hebrew word inside English text;
- digits and a Latin word inside a right-to-left paragraph, both keeping
  their order;
- mirrored brackets;
- plain left-to-right text left untouched;
- a space on the paragraph side;
- digits after Hebrew in a left-to-right paragraph (`AB 12` shows as
  `12 BA`);
- the resolved levels.

Negative control: skipping the final level-1 pass fails with code 1.

Not covered:
- explicit embeddings or isolates (LRE/RLI…);
- Arabic-Indic digits and number separators (ES/CS/ET);
- line breaking by levels;
- shaping (Arabic joining forms);
- wiring into the overlay text renderer, which still draws bytes in
  logical order.

## Course captions in display order (2026-09-30)

Wicked's font renderer draws glyphs left to right in byte order and does no
bidi of its own. `src/ui/bidi_utf8.elisa` (`UiBidiUtf8`) therefore works on
wrapped UTF-8 one line at a time: it decodes each line (malformed bytes
become U+FFFD), orders it with `UiBidi` and encodes it again. A line over 64
codepoints is passed through unchanged. The course's `wrapped_caption`
applies it after wrapping, with a right-to-left paragraph for Arabic.

Tests:
- `test/ui_bidi_utf8.elisa` (in the gate's unit-test list) covers Hebrew in
  a two-line caption in both paragraph directions, a stray continuation
  byte and empty text. Control: joining lines at the newline fails with 2.
- Course code 172 checks that the Arabic jump caption reaches the renderer
  with the same byte count but reordered, and that English passes through
  unchanged. Control: skipping the reorder in `wrapped_caption` fails the
  course smoke with 172.

Arabic letter shaping (joining forms) is still missing.

## Arabic shaping (2026-09-30)

`src/ui/arabic_shaping.elisa` (`UiArabicShaping`) turns U+0621..U+064A into
isolated, final, initial or medial presentation forms (U+FE80..U+FEF4). It
also makes the four lam-alef ligatures (U+FEF5..U+FEFC). Harakat are
transparent, hamza never joins, and tatweel joins on both sides.
`UiBidiUtf8` shapes each line in logical order before `UiBidi` reorders it.
Course code 172 now accepts that the byte count changes.

Tests:
- `test/ui_arabic_shaping.elisa` (in the gate's unit-test list) covers
  joining, right-joining letters, both ligature forms, transparency, hamza,
  a space break and tatweel.
- Every letter's four form slots were checked against Python's
  `unicodedata` names, with no mismatches.
- `test/ui_bidi_utf8.elisa` case 8 checks that shaped Arabic is then
  reversed.
- Control: treating harakat as letters fails with code 7.
- Both course smokes pass.

**Open:** Wicked's only built-in font is Liberation Sans, which has no
Arabic glyphs, so shaped Arabic captions still render without glyphs. An
Arabic-capable font has to be bundled and registered with
`wi::font::AddFontStyle`. Persian and Urdu letters outside U+0621..U+064A
are left unshaped.

## Arabic fallback font

`RenderScene::add_overlay_font(path)` (native `elisa_render_scene_v1_add_font`)
registers a project-relative `.ttf`/`.otf` asset as a Wicked font style. Wicked
falls back across styles for glyphs the default Liberation Sans lacks. The call
rejects files that are not TrueType or OpenType, and it clears the
overlay-measure cache so metrics measured before the font was registered are
dropped. The character course bundles Noto Naskh Arabic UI
(`fonts/NotoNaskhArabicUI-Regular.ttf`, SIL OFL 1.1; the notice is in
`third_party/notices/NotoNaskhArabic-OFL.txt`) and registers it right after
renderer initialization.

Course code 173 compares a run of lam-alef ligatures (U+FEFB) with a run of
unassigned private-use points (U+E000). Without the font both runs measure as
identical missing-glyph advances, so they differ in width only when the font is
registered. The first version of the check, which only required a width above
3 px, passed with no font registered; the control without registration now
fails with 173.

## Persian and Urdu letters

`UiArabicShaping` also shapes peh, tcheh, jeh, keheh, gaf, farsi yeh, tteh,
ddal, rreh, noon ghunna, heh goal, heh doachashmee and yeh barree. Their forms
come from Arabic Presentation Forms-A (U+FB50..), in the same isolated, final,
initial, medial order as the Forms-B block. Every slot was checked against
Python `unicodedata` decompositions, and the bundled Noto Naskh Arabic UI covers
the glyphs used. Superscript alef (U+0670) is treated as transparent.
`test/ui_arabic_shaping` cases 10–11 cover peh/jeh/gaf, a medial farsi yeh and
Urdu heh goal + yeh barree. Negative control: dropping farsi yeh's base fails
with 11. Not covered: other extended letters, such as those for Pashto, Sindhi
or Kurdish.
