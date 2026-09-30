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
