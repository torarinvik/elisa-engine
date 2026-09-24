# Post-process quality validation

`src/backend/quality.elisa` is the renderer-neutral profile contract. It keeps
quality selection in Elisa, validates render scale, bloom thresholds, shadow
quality and sun receiver bias, and offers bounded low/medium/high presets.
`native/postprocess_bridge.h` translates
the contract to Wicked's `RenderPath3D` controls for tonemapping, bloom, FXAA,
temporal AA, SSAO, SSR, height fog, depth effects, and render scale. Low
quality disables fog along with the more expensive effects; invalid scale and
thresholds are rejected before any native state changes. FSR1/FSR2 are enabled
only when the queried adapter says they are supported; otherwise the adapter
returns an explicit fallback after applying the rest of the profile. Temporal
AA uses Wicked's renderer history path; Elisa profiles reject enabling it
alongside FSR2, which already owns a temporal reconstruction pass.

Evidence: the shared Elisa gate runs `test/quality.elisa`; the SDL3/Wicked gate
calls `probe_postprocess_bridge` and checks both fallback and supported paths,
including fog and temporal-AA state. The probe restores the original scene
weather after the check. The render-scene smoke also verifies both TAA history
textures match the active internal resolution after a 0.75-to-0.5 render-scale
transition, are released when TAA is disabled, and are recreated at the new size
when TAA is re-enabled. Measured GPU/VRAM costs remain open R07 work.

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

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" ELISA_RENDER_SCENE_RENDER_ONLY=1 /opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py` — SDL3/Metal render-scene smoke exited 0, including the history-resource transitions and authored lighting/material image checks.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` — full shared suite exited 0; both Elisa Proof suites proved all 23 obligations with certificate replay.
- `/opt/homebrew/bin/python3 scripts/check_source_length.py`, `/opt/homebrew/bin/python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

This verifies setting changes reach Wicked's runtime state; authored image
references and measured GPU/VRAM costs remain R07 work.
