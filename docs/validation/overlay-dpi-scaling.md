# Overlay DPI scaling

Overlay text and panels are placed in logical units. The native application
initialises the Wicked canvas with `dpi = display_scale * 96`, and `wi::font`
draws through the canvas projection, so a logical size becomes
`size * display_scale` physical pixels.

## Evidence

`elisa_render_scene_v1_last_frame_overlay_text_fit(size)` in
`native/render_scene_pixel_probe.h` reads back the 2D render result inside the
logical rect 0..200 x 0..60 (converted with `LogicalToPhysical`), finds the top
and bottom lit text rows and divides by `GetDPIScaling()`. It fails when the
logical height is below 0.4x or above 1.3x the font size, and when no text is
drawn. The render scene smoke (`test/render_scene_native_main.elisa`, case 79)
runs it on the "LIVES  3" HUD at size 24.

On 2026-10-02 on a Retina display the smoke reported 23 physical rows at
scaling 2.000 (11.50 logical). As a negative control, forcing the canvas to
96 dpi gave 12 physical rows at scaling 1.000 (12.00 logical): the physical
height halves and the logical height holds, so text follows the display scale.

## Gaps

- Only one 2x display was measured; fractional scales (1.25, 1.5) and moving a
  window between displays of different scale were not run.
- The check measures the lit cap-to-baseline span of an all-caps label, not
  the font's full line height.
