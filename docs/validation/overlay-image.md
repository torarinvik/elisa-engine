# Wicked screen-space image overlays

`RenderScene::create_overlay_image` draws a project-bounded image as a
screen-space sprite on the persistent Wicked `RenderPath3D` 2D pass, in the
scene's logical canvas coordinates. It returns a generation-checked
`OverlayImageHandle`; the public Elisa surface supports position, size,
normalized atlas UV selection, RGBA tint, visibility, and destruction. No
Wicked texture or sprite object is exposed.

The native adapter bounds the service to 64 live images. Asset paths pass
through `elisa::assets::resolve_project_asset_path`, so absolute paths, `..`
segments, backslashes, and missing files are rejected before a live slot is
published. Rectangles must be finite, positive, and inside the viewport limit;
UV rectangles must satisfy `0 <= u0 < u1 <= 1` and the same for `v`; colors must
be finite and within 0–1. The sprite uses clamp addressing and alpha blending.
Handles reject stale generations, and every operation requires the application
owner thread. Scene reset releases the texture resource and invalidates every
outstanding handle.

`test/render_scene_image_native.elisa` runs in group 229 alongside the panel
smoke. It rejects missing and escaping paths, invalid rectangles, positions,
UVs, and colors, then loads `test/fixtures/glyph_atlas.png`, selects its blue
bottom-right swatch, and checks the blue-dominant rendered pixel after SDL3 and
Metal frames. It hides and restores the image, destroys it, and verifies stale
handle rejection. The pixel readback probe is compiled only by the native
smoke.

Validation on 2026-09-22:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 /opt/homebrew/opt/python@3.14/bin/python3.14 scripts/render_scene_native_smoke.py` passed against SDL3, Metal, and Wicked.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

This is the image-overlay base for I02. Nine-slice panels, retained layout,
clipping, font shaping, editable text, and accessible semantics remain open.
