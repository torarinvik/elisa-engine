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
backend. It ran three frames and passed menu construction, disabled-item focus
skipping, scroll adjustment, relayout and restyling, invalid font-size
rejection, and destruction of every retained text and panel handle. The
`scripts/check.elisascript` full gate also exited successfully after this
integration. `scripts/check_module_hygiene.py` and
`scripts/check_dependency_manifest.py` passed.

The broader `scripts/render_scene_native_smoke.py` cooked and linked, but its
full render-scene app exited 134 immediately after Wicked logged creation of
its first 256 MiB GPU buffer. The process stopped before the native scene
assertions, so that broader set remains unverified. The isolated UI main above
does not hit that mesh/GPU-buffer path.

## Limits

The adapter uses logical pixel coordinates and the existing default font. The
smoke exercises the real Wicked path but does not compare captured pixels for
this menu. Clipping, DPI scaling, text shaping/measurement, wrapping, and
dynamic label ownership are not implemented. Menu labels are static `cstr`
values and must outlive the menu; callers should use RenderScene text updates
for runtime values.
