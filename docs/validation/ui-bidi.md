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
