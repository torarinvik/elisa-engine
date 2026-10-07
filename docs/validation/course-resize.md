# Course resize and capture memory — 2026-10-07

## Implemented

The course keeps the chosen text size and pages the controls rows to fit the
logical window height. Keyboard/wheel selection brings the selected row into
view; pointer presses translate visible slots through the same page policy.
The caption and contrast panel follow the visible rows. Layout updates on
height, selection or settings changes, including while paused and in controls.
Camera projection continues to resize using drawable dimensions.

The full live-input pilot now starts at 1280×720, resizes the real SDL window
to 600×400 while paused, resumes, restores 1280×720 during play and verifies
logical dimensions and nonzero drawable dimensions. It then completes the
step/crouch/jump/win/restart/fall route. Starting at the largest tested size
lets the existing streaming-memory check warm render resources first.

## Evidence

- `character-course-live-input-smoke`: status 0, 230.790 seconds including
  build/link, SDL3/Metal, silent audio fallback. Its original streaming
  allowances remain 8 MiB GPU and 16 MiB CPU above warm peaks.
- `application-async-capture-smoke`: status 0; resize-safe asynchronous output
  decoded at 880×560, confirming the shared encoder on mapped async readback.
- `test/character_course_menu_view.elisa`: passes all 16 selections at every
  page size from 1 through 16, plus invalid slots and capacity clamping.
- `proof/character_course_menu_view.elisa`: 20/20 obligations and all 20 kernel
  certificates replayed. This covers the implementation's pure page/hit
  policy, not rendering. Report `build/character-course-menu-view-proof.json`,
  SHA256 `27cdb940cb4888736a716fd7e8c89c9bf20bd4b71b03537fddbad619256b1c88`.
- Win/fall PNGs are decoded by the smoke runner and retained under
  `build/validation/character-course-presentation/`. The win image was inspected
  at 2560×1440: the summit instruction and all sixteen rows are readable.

## Small controls through SDL

The course pilot opens controls at 600×400, wraps keyboard selection from row
0 to row 15, then clicks the top visible slot at 100% and asserts row 5 was
selected. It selects Text size and changes it to 125% and 150%; at 150%, a
click on the top visible slot must select row 1. It wraps to row 15 again,
restores 100%, closes controls and resumes the complete route. Clicks are
synthetic SDL button-down/up events translated by the ordinary pointer queue.

Four retained images (`menu-100.png`, `menu-125.png`, `menu-150.png` and
`menu-bottom.png`) are 1200×800 on this display. All were inspected: labels,
values, selection highlighting and the complete hint fit. The old hint was
clipped; it now reads `Up/Down select | click or type | Tab done`. Captures wait
eight frames for glyph rendering to settle after size/selection changes;
early images at new font sizes had omitted glyphs or no HUD text.

The runner requires all four PNGs, matching dimensions with the small-window
aspect ratio, visible bright text and changed images. These pixel checks do
not replace the visual review. Seven runner tests passed, including missing,
unchanged, wrong-size and scene-only rejection cases. The existing pure page
proof remains 20/20; this run establishes its native SDL consumer path.

## Allocation defect exposed by larger captures

The first failing memory sample grew from 946931128 to 1035929112 CPU bytes
while GPU usage fell. The PNG encoder held complete filtered, DEFLATE and PNG
buffers in addition to readback copies. Streaming stored DEFLATE blocks cut
out those image copies, but the synchronous Wicked helper still copied the
image; a later run grew from 947127712 to 970769896 CPU bytes and still failed.
Encoding directly from mapped staging memory then passed the unchanged bound.

`native/rgba_png.h` now uses one 65535-byte pixel block plus small metadata and
stream buffers. `native/png_capture.h` copies the GPU texture into a mapped
staging texture, restores its resource state, waits for completion and encodes
without full CPU image copies. The CPU PNG self-test covers channel conversion,
row padding, CRCs, exact and multiple DEFLATE blocks, and asserts no individual
encoder allocation exceeds 65535 bytes. The pre-staging failure report/log are
retained under `build/validation/course-resize/before-direct-staging.*`.

## Remaining acceptance

Synthetic SDL clicks and resizing do not establish physical pointer input,
user-driven resizing, DPI/display changes or an OS focus switch. Tiny windows
that cannot fit a header, row and caption still need a minimum-size/layout
decision. Automated acceptance of the named 600×400 and 1280×720 resize slice
is complete; those external and tiny-window cases remain in the full backlog.
