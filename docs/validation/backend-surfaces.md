# Render surface table

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R18 progress. It is the handle and lifecycle policy only; no SDL3
window or Wicked swapchain is created from it.

## Design

`src/backend/surfaces.elisa` (`BackendSurfaces`) holds four surfaces, each with
its own size (up to 16384), DPI (500–4000 permille) and suspension. A `Handle`
is a slot plus generation. Opening takes the first free slot and bumps its
generation; bad size/DPI or a full table returns `none()`. Resize, DPI,
suspend and close on a stale, closed or out-of-range handle return false and
change nothing. Closing one surface bumps only its own generation, so the
others stay valid and renderable; a reused slot never revalidates the old
handle. `pixel_width` gives the DPI-scaled swapchain width.

## Checks

- `test/backend_surfaces.elisa` exits 0 (codes 1–14): an editor and a game
  surface open together, sizes and DPI, independent resize and suspend, close of
  one leaving the other, stale-handle rejection, slot reuse, full table.
- Negative control: never bumping a slot generation (open or close) makes it exit 11 (a reused slot revalidates the stale handle).
- Source-length check passes.

## Gaps

- No native windows or swapchains, no simultaneous render of two surfaces and no
  close-during-frame test, so R18's done condition is not met. Full gate still
  blocked at `world-test`.
