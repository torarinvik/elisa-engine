# Retained menu renderer validation

**Date:** 2026-09-23
**Scope:** Elisa menu, layout, and style models rendered through RenderScene's
retained screen-space panels and text on the native SDL3/Metal/Wicked backend.

## Evidence

The focused native smoke passed on macOS with the installed Wicked and SDL3
libraries:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_RENDER_SCENE_RENDER_ONLY=1 \
ELISA_RENDER_SCENE_NATIVE_MAIN=test/ui_renderer_native_main.elisa \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 \
/opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

The ordinary Elisa main compiled to an archive and linked with the native
backend. It ran three frames and passed construction, disabled-item focus
skipping, scroll adjustment, relayout and restyling, invalid font-size
rejection, and destruction of every retained text and panel handle. The menu
model and renderer canvas now hold up to 24 items; the native smoke renders an
18-item page through both its scrolled window and its full-page layout. The
targeted maze-game fixture also passes 18-item focus traversal, scrolling and
hit testing. `scripts/check_module_hygiene.py` and
`scripts/check_source_length.py` passed.

The same focused smoke verifies that a repeated text measurement stays
unready during the request frame, then measures Wicked `SpriteFont` text at two
logical sizes after a rendered atlas warm-up frame; it also checks rejection of
an invalid size. It lays out a four-word `UiText::Run` into three lines through
`UiRenderer::sync_wrapped`, then verifies that syncing an empty run hides the
retained word labels. `test/editor.elisa` passes the word-run, line-boundary,
invalid-wrapped-data, and maximum-line-capacity checks.

The full `elisascript scripts/check.elisascript` gate passed on 2026-09-23 when
run with Homebrew Python 3.14 first on `PATH`. The default Xcode Python lacked
both `compression.zstd` and a discoverable `libzstd`, causing the package test
to fail after the runtime fixtures had passed; selecting Python 3.14 resolved
that environment dependency. Module hygiene, source length, and the native
dependency manifest checks also passed.

The broader `scripts/render_scene_native_smoke.py` cooked and linked, but its
full render-scene app exited 134 immediately after Wicked logged creation of
its first 256 MiB GPU buffer. The process stopped before the native scene
assertions, so that broader set remains unverified. The isolated UI main above
does not hit that mesh/GPU-buffer path.

## Limits

The adapter uses logical pixel coordinates and the existing default font. The
smoke exercises the real Wicked path but does not compare captured pixels for
this menu. `measure_overlay_text` reports `ready = false` until Wicked has
processed the requested glyphs through its dynamic atlas; callers must retry
after a render frame. Font selection, clipping, DPI scaling and text shaping
remain open. Wrapped runs currently take already-tokenized words and one
retained overlay handle per word. Menu and wrapped-word labels are static
`cstr` values and must outlive their models; runtime wording can be refreshed
with RenderScene text updates.
