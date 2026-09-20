# Wicked screen-space text overlays

`RenderScene` exposes generation-checked `OverlayTextHandle` values for
screen-space labels. The public Elisa surface supports create, text replacement,
signed-integer formatting, visibility changes, and destruction. It exposes no
Wicked font object or pointer. Text is submitted to the persistent Wicked
`RenderPath3D` 2D pass and uses the scene's logical canvas coordinates.

The native adapter bounds the service to 32 live labels and 256 bytes per
label. It passes text bytes through as supplied; Unicode shaping and text
encoding validation are not implemented yet. Font sizes are 8–128 pixels,
positions are bounded by the viewport coordinate limit, and RGBA values must
be finite and within 0–1. Integer formatting reserves 20 bytes for a signed
64-bit decimal value. Handles reject stale generations; all operations require
the application owner thread. Scene reset clears fonts and invalidates
outstanding handles before Wicked device teardown.

`test/render_scene_native_main.elisa` checks invalid font input, text and
integer updates, visibility changes, rendered 2D pixels, destruction, stale
handles, and invalidation after scene replacement. The pixel readback probe is
compiled only by the native smoke; ordinary game builds do not include its
blocking GPU readback.

Validation on 2026-09-20:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/render_scene_native_smoke.py` passed on the merged engine tree with SDL3, Metal, and Wicked.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `python3 scripts/check_dependency_manifest.py` passed.

This is the text-rendering base for I02. Layout integration, clipping,
scrolling, keyboard/controller focus, hit testing, Unicode shaping, editable
text, and accessible semantics remain in I02 and I04–I07.
