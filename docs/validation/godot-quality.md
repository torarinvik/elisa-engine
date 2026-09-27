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
scene under both profiles, checks that at least 1,024 pixels and 0.02 mean RGB
change, and compares each frame to its checked-in reference with peak/mean
limits of 0.12/0.01. Run it on the tested host with:

```sh
GODOT_BIN=godot python3 scripts/godot_quality_visual_smoke.py
```

On Godot 4.7.2 / macOS 27 / Apple M5 using Compatibility, the profiles changed
all 64,000 pixels at 320×200, with 0.4522 mean RGB difference. Both captures
matched their references exactly in repeated runs. The Low and High images are
[`low.png`](references/godot-quality/low.png) and
[`high.png`](references/godot-quality/high.png). Per-profile GPU/memory
measurements and Forward+/Mobile runtime captures remain open.
