# UI clipping, scrolling and clipped hit testing

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I02 progress.

## Design

- `src/ui/clip.elisa` (`UiClip`) keeps a bounded clip stack, at most 8 deep.
  The root is the screen and can't be popped. Each push stores the
  intersection with the current clip, so a child can't widen an ancestor's
  clip. Negative sizes and pushes past the depth bound are refused.
- `hit` accepts a point only if it lies in both the widget rectangle and the
  active clip, so a partly hidden button is hit only on its visible part.
- `Scroll` clamps its offset to `[0, content − viewport]`. `reveal` scrolls the
  least distance that shows a focused span and aligns the top of a span taller
  than the viewport. `to_screen` maps content rows to screen rows.

## Checks

- `test/ui_clip.elisa` exits 0 (codes 1–22). It covers nested intersection,
  partial-button hits, disjoint clips, pop and root protection, refusal of
  negative sizes, the depth bound, scroll clamping, reveal in both
  directions, oversize spans, and content shorter than the viewport. It is in
  the gate's asset-test list.
- Negative control: taking the larger right edge in `clip_intersect` makes the
  test exit 4.

## Gaps

- `UiRenderer::sync_menu_clipped` draws a menu inside a clip box: panels are
  trimmed (hidden when empty) and a label shows only when its whole row is
  inside. The native smoke exercises a cutting clip and an off-screen clip,
  but it cannot read the overlay state back, so it only shows these calls
  are accepted. Overlays have no GPU scissor, so text that crosses the clip
  is hidden rather than cut, and the HUD does not push panels yet.
- Focus traversal lives in ui-focus-nav.md. I02 stays open.

## Proof

`proof/ui_clip.elisa` checks the pure arithmetic in `src/ui/clip_math.elisa`
with 0 findings: overlap widths are never negative for coordinates within
±1e9, and scroll offsets stay within [0, max scroll]. `clip_intersect` and
`new_scroll` reject inputs outside those bounds, so the proven range covers
every value they pass in.
