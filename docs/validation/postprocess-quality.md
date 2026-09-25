# Post-process quality validation

`src/backend/quality.elisa` is the renderer-neutral profile contract. It keeps
quality selection in Elisa, validates render scale, bloom thresholds, shadow
quality and sun receiver bias, and offers bounded low/medium/high presets.
`native/postprocess_bridge.h` translates
the contract to Wicked's `RenderPath3D` controls for tonemapping, bloom, FXAA,
temporal AA, SSAO, SSR, height fog, depth effects, and render scale. Low
quality disables fog along with the more expensive effects; invalid scale and
thresholds are rejected before any native state changes. FSR1/FSR2 are enabled
only when the active device supports the required storage formats and Wicked
has loaded every shader pass for that path; otherwise the adapter returns an
explicit fallback after applying the rest of the profile. The capability probe
runs once when the render scene starts and is shared by primary and secondary
cameras. FSR1 also requires a render scale below 1 and an internal resolution
smaller than the display; FSR2 rejects inputs smaller than 2x2 because Wicked's
resource setup builds a half-resolution luminance pyramid. Temporal AA uses
Wicked's renderer history path; Elisa profiles reject enabling it alongside
FSR2, which already owns a temporal reconstruction pass.
`RenderScene::apply_quality_profile_with_result` and
`set_camera_quality_profile_with_result` return
`Quality::ApplyOutcome.UpscalerFallback` when Wicked disables a requested
upscaler, while the compatibility wrappers retain their previous void result.
The SDL3/Wicked adapter now negotiates FSR1 and FSR2 separately. FSR1 checks the
main render-target storage format and both FSR1 shader handles. FSR2 checks the
device's common UAV-load capability, all formats used by
`CreateFSR2Resources`, and all eight FSR2 compute shader handles. Format probes
create small SRV/UAV textures during scene initialization and immediately
release them, so profile changes do not repeat device allocations.

Evidence: the shared Elisa gate runs `test/quality.elisa`; the SDL3/Wicked gate
calls `probe_postprocess_bridge` and checks per-path capability mapping,
unsupported fallback, and the remaining profile state, including fog and
temporal AA. The probe restores the original scene weather after the check. The
result-return smoke forces a deterministic FSR1
fallback by requesting it at native resolution, then restores the prior profile.
The render-scene smoke also verifies both TAA history
textures match the active internal resolution after a 0.75-to-0.5 render-scale
transition, are released when TAA is disabled, and are recreated at the new size
when TAA is re-enabled.

The same smoke captures the High and Low profiles after three settling frames,
then restores the prior profile and rechecks its TAA history. The checked-in
references in `docs/validation/references/postprocess/` are SDL3/Metal captures
reduced to 160x100 with nearest-neighbour sampling. The comparison uses the
shared image metric with per-channel peak tolerance 0.35 and mean tolerance
0.025, allowing device-level shading variation while detecting missing effects
or a profile rendered with the wrong settings. The Low reference visibly uses
the lower render scale; the High reference retains the full internal detail.
These images validate rendered profile output.

The 2026-09-25 quality-result smoke adds Elisa assertions for the scene and
camera fallback outcomes and verifies their other profile fields. The
capability-negotiation probe also covers per-upscaler availability, fallback
state, full-resolution FSR1 rejection, and invalid temporal combinations. The
targeted stage1 build emits the Elisa archive and compiles the native sources,
but cannot link the executable: the seeded compiler omits `arena_free`,
`ctx_streq`, and `ctx_string_views_eq`, and this build target does not export the
scene test-probe functions. Runtime assertions therefore remain unverified
until those gates link.

## Apple M5 profile cost sample

The focused SDL3/Metal cost smoke starts a fresh process for each preset. It
uses the same hidden 320×200 window, camera, box/sphere/floor scene, and
baseline quality checks, waits ten frames after applying a preset, and samples
21 completed frames. GPU time is Wicked's timestamp interval around
`RenderPath3D::Render()`. Memory is Wicked's Metal `currentAllocatedSize()`
reported for the application; it includes renderer, scene, and engine
allocations, so the total is a scene-level working-set reference rather than
the isolated cost of each effect.

One run on an Apple M5 with macOS 27.0 produced:

| Profile | GPU median / p95 | Steady allocation median | Sample peak | Peak rise during samples |
| --- | ---: | ---: | ---: | ---: |
| Low | 938 / 945 μs | 397.59 MiB | 397.78 MiB | 0.72 MiB |
| Medium | 1,342 / 1,392 μs | 404.80 MiB | 405.08 MiB | 0.91 MiB |
| High | 2,777 / 2,808 μs | 418.39 MiB | 418.95 MiB | 1.47 MiB |

These are reference measurements for this small scene and device. Render-path
times vary with GPU load; memory totals include the common baseline allocations
and should not be read as per-effect deltas. Other GPU backends and larger game
scenes still need measurements.

The public `RenderScene` API also exposes direct SSAO and FXAA toggles for
applications that manage a scene without the backend profile bridge. The
SDL3/Metal render-scene smoke enables both, checks their Wicked `RenderPath3D`
state, disables both and checks the restored state, then verifies malformed
integer flags are rejected at the C ABI boundary. Validation passed:

`RenderScene::apply_quality_profile` carries the validated Elisa
`Quality::Profile` contract through the public Elisa runtime. It applies
tonemap, render scale, bloom threshold/toggle, FXAA or temporal AA, SSAO, SSR,
height fog, depth effects, shadow atlas tier and sun receiver bias in one
checked native call; an unsupported FSR request keeps the rest of the profile
and returns the documented fallback as success. The additive native entry
point preserves the original v1 profile struct layout. The native quality
fixture checks the applied Wicked state and rejects invalid scale, shadow tier
and receiver bias before changing native state.

`RenderScene::set_shadow_quality` remains available as a direct control. The
Low/Medium/High presets select 512/1024/2048-pixel 2D maps and a
quarter-resolution cube map; profile presets include the same shadow tier.

To refresh the profile references from a native capture, set
`ELISA_UPDATE_POSTPROCESS_REFERENCES=1` for the render-scene smoke. To compare
against the checked-in images without changing them, run
`python3 scripts/compare_postprocess_references.py` after the smoke. The
comparison script also accepts `--capture-dir`, `--reference-dir`, and
`--update` for focused use.

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" ELISA_RENDER_SCENE_RENDER_ONLY=1 ELISA_UPDATE_POSTPROCESS_REFERENCES=1 /opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py` — builds High/Low captures, updates the reduced references, and runs all render-scene assertions and image comparisons.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" ELISA_RENDER_SCENE_RENDER_ONLY=1 ELISA_RENDER_SCENE_PROFILE_COST_ONLY=1 /opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py` — builds the focused probe and measures each preset in its own SDL3/Metal process.
- `/opt/homebrew/bin/python3.14 scripts/compare_postprocess_references.py` — compares the most recent High/Low captures against the checked-in references.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` — full shared suite exited 0; both Elisa Proof suites proved all 23 obligations with certificate replay.
- `/opt/homebrew/bin/python3 scripts/check_source_length.py`, `/opt/homebrew/bin/python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

This verifies profile changes reach Wicked's runtime state and rendered output,
and records one Apple M5 GPU/VRAM reference sample. Cross-backend cost
validation and settings persistence beyond the game setting remain open R07
work.
