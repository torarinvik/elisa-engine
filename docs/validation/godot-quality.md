# Godot quality profiles

`backends/godot/quality_service.gd` adapts the validated Elisa quality profile
to a Godot `Viewport`, `Environment`, and optional `CameraAttributesPractical`.
The dictionary keys use Elisa's profile field names and enum values as lowercase
strings. Invalid profiles and missing targets are rejected before any resource
is modified. A valid profile applies every supported setting and returns the
unsupported settings by name; an unavailable FSR mode reports
`upscaler_fallback` while retaining the requested render scale where Godot can
apply it.

The adapter maps Reinhard and ACES tonemapping directly. Godot has no Uchimura
curve, so that request uses the built-in FILMIC curve and reports the
approximation. Bloom, fog, shadow-atlas tier, and supported viewport effects
map to Godot resources. Elisa's normalized reverse-Z shadow receiver bias is
reported as unsupported because Godot's light-space bias uses different units.
The result marks an upscaler failure as `upscaler_fallback`, other unsupported
features as `feature_fallback`, and a curve substitution as `approximation`;
each result also carries the exact unsupported or approximated fields.
Godot's renderer capability matrix controls scale, upscaling, antialiasing,
occlusion, reflections, and depth-of-field requests; see the [Godot 4.7 renderer
overview](https://docs.godotengine.org/en/4.7/tutorials/rendering/renderers.html).

The headless contract probe checks profile validation and failure atomicity,
the Compatibility/Mobile/Forward+ capability matrix, supported setting
application, unsupported-setting reporting, and the Uchimura approximation.
Run it directly with:

```sh
godot --headless --path backends/godot --script res://quality_probe.gd
```

It is also part of `scripts/check.elisascript`. This is adapter contract
coverage. A display-backed Low/High smoke renders the same emissive material
scene under both profiles and checks that at least 1,024 pixels and 0.02 mean
RGB change. Compatibility additionally compares each frame to its checked-in
reference with peak/mean limits of 0.12/0.01. The capture disables VSync,
warms up 12 frames per profile, then samples 60 frames for CPU/GPU timing and
renderer-reported video memory. Run Compatibility and the two RenderingDevice
backends on the tested host with:

```sh
GODOT_BIN=godot python3 scripts/godot_quality_visual_smoke.py
GODOT_BIN=godot GODOT_RENDERING_METHOD=mobile python3 scripts/godot_quality_visual_smoke.py
GODOT_BIN=godot GODOT_RENDERING_METHOD=forward_plus python3 scripts/godot_quality_visual_smoke.py
```

Each run writes a machine-readable report under `build/godot-quality/`. On
Godot 4.7.2 / macOS 27 / Apple M5 at 320×200, all three renderers changed all
64,000 pixels between Low and High. Compatibility matched both references
exactly; its mean Low/High RGB difference was 0.4522. The Low and High images
are [`low.png`](references/godot-quality/low.png) and
[`high.png`](references/godot-quality/high.png).

| Renderer | Low CPU p50 / p95 / p99 (ms) | High CPU p50 / p95 / p99 (ms) | Low / High video memory (MiB) | GPU timing |
| --- | --- | --- | ---: | --- |
| Compatibility | 0.288 / 0.459 / 0.600 | 0.985 / 1.501 / 1.715 | 6.88 / 7.70 | Unsupported by renderer |
| Mobile | 0.106 / 0.145 / 0.153 | 0.085 / 0.125 / 0.232 | 15.00 / 79.67 | No samples returned |
| Forward+ | 0.084 / 0.117 / 0.167 | 0.096 / 0.207 / 0.250 | 21.70 / 94.89 | No samples returned |

The sample reports are
[`Compatibility`](../../build/godot-quality/measurements-gl_compatibility.json),
[`Mobile`](../../build/godot-quality/measurements-mobile.json), and
[`Forward+`](../../build/godot-quality/measurements-forward_plus.json). GPU
timing is unavailable on Compatibility; on the tested Apple Metal Mobile and
Forward+ renderers, Godot returned zero GPU samples for both profiles. Video
memory is a renderer-wide reading after each profile, including shared and
retained allocations; the difference is not an isolated cost for a single
quality feature. CPU/GPU timings can vary with device state. FSR2 applies on
Forward+ and falls back on Compatibility and Mobile. GPU-time measurements on a
supported renderer/driver remain open.

## Medium profile and memory per level (2026-10-02)

`quality_capture.gd` now also measures a Medium profile that mirrors
`Quality::Profile()`: ACES, bloom and fog on, no SSAO, medium shadows. Medium is
applied after the Low and High images are saved, so both references are
unchanged. `godot_quality_visual_smoke.py` validates CPU samples and video
memory for all three levels. The display-backed run used Godot 4.7.2,
macOS 27 and an Apple M5 at 320×200. All three renderers passed, and
Compatibility still matched its references exactly (peak/mean 0.0000).

| Renderer | CPU p50 Low / Medium / High (ms) | Video memory Low / Medium / High (KiB) | GPU timing |
| --- | --- | ---: | --- |
| Compatibility | 0.178 / 0.549 / 0.714 | 7,046 / 7,879 / 7,879 | Unsupported by renderer |
| Mobile | 0.064 / 0.047 / 0.056 | 14,784 / 31,472 / 81,008 | No samples returned |
| Forward+ | 0.084 / 0.056 / 0.096 | 22,224 / 39,024 / 97,088 | No samples returned |

`QualityCost::cost` carries these memory values. Memory is a renderer-wide
reading after each profile. Because Medium is measured after High, its reading
may include allocations retained from High; on Compatibility it equals High.
Treat it as an upper bound, not a per-feature cost.

GPU time: the capture runs with a display (no `--headless`), enables
`RenderingServer.viewport_set_measure_render_time` on the root viewport, and
reads `viewport_get_measured_render_time_gpu` after `frame_post_draw` for 60
frames after 12 warm-up frames. On Forward+ and Mobile (Metal) every reading
was zero, as in the 2026-09-27 run. Compatibility has no GPU timer. This is a
limitation of the backend on this host. The cost records report it explicitly,
and Wicked/Metal provides the measured GPU times.
