# Wicked screen-space image overlays

`RenderScene::create_overlay_image` draws a PNG from the project's asset tree
as a screen-space sprite on the persistent Wicked `RenderPath3D` 2D pass, in the
scene's logical canvas coordinates. It returns a generation-checked
`OverlayImageHandle`; the public Elisa surface then supports position, size,
UV sub-rectangle (`OverlayImageUv`, normalised 0–1 so an atlas cell can be
selected without reloading), RGBA tint/opacity, visibility, and destruction. No
Wicked texture or sprite object is exposed.

The native adapter bounds the service to 64 live images. Asset paths pass
through `elisa::assets::resolve_project_asset_path`, so absolute paths, `..`
segments and backslashes are rejected before any file access, and a missing
file reports `AssetLoadFailure`. Rectangles must be finite, positive and inside
the viewport coordinate limit; UV rectangles must satisfy `0 <= u0 < u1 <= 1`
(and the same for v); colors must be finite and within 0–1. The sprite samples
with clamp addressing and alpha blending. Handles reject stale generations;
all operations require the application owner thread. Scene reset hides and
releases every image before Wicked device teardown.

`test/render_scene_image_native.elisa` (run from the panel group, exit 229)
rejects a missing file and an escaping path, rejects a zero-size rectangle,
creates an image from `test/fixtures/glyph_atlas.png`, exercises each setter
with valid and invalid values and a stale handle, selects the atlas's solid
blue probe cell through the UV setter, pumps frames and samples the rendered
2D result at the image's position (blue-dominant pixel), hides it and confirms
the pixel goes away, then destroys it. The pixel readback probe is compiled
only by the native smoke.

Validation on 2026-09-22:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools WICKED_BUILD=../WickedEngine/build-elisa-sdl3-release /opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py`
  passed on macOS with SDL3, Metal, and Wicked, including the new image group.
- `python3 scripts/check_source_length.py` and `python3 scripts/check_module_hygiene.py` passed.
- The Amazing Labyrinth help card draws keyboard and gamepad prompts as atlas
  glyphs beside their labels through this API (game plan item P05.3).

This is the image-overlay base for I02. Nine-slice panels, layout integration,
clipping and animated sprites remain open.
