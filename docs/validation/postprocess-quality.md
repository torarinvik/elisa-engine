# Post-process quality validation

`src/backend/quality.elisa` is the renderer-neutral profile contract. It keeps
quality selection in Elisa, validates render scale and bloom thresholds, and
offers bounded low/medium/high presets. `native/postprocess_bridge.h` translates
the contract to Wicked's `RenderPath3D` controls for tonemapping, bloom, FXAA,
SSAO, SSR, height fog, depth effects, and render scale. Low quality disables
fog along with the more expensive effects; invalid scale and thresholds are
rejected before any native state changes. FSR1/FSR2 are enabled only when the
queried adapter says they are supported; otherwise the adapter returns an
explicit fallback after applying the rest of the profile.

Evidence: the shared Elisa gate runs `test/quality.elisa`; the SDL3/Wicked gate
calls `probe_postprocess_bridge` and checks both fallback and supported paths,
including fog state. The probe restores the original scene weather after the
check. History-resource resizing and measured GPU/VRAM costs remain open R07
work.

The public `RenderScene` API also exposes direct SSAO and FXAA toggles for
applications that manage a scene without the backend profile bridge. The
SDL3/Metal render-scene smoke enables both, checks their Wicked `RenderPath3D`
state, disables both and checks the restored state, then verifies malformed
integer flags are rejected at the C ABI boundary. Validation passed:

`RenderScene::apply_quality_profile` now carries the validated Elisa `Quality::Profile`
contract through the public Elisa runtime. It applies tonemap, render scale,
bloom threshold/toggle, FXAA, SSAO, SSR, height fog and depth effects in one
checked call; an unsupported FSR request keeps the rest of the profile and
returns the documented fallback as success. The native quality fixture checks
the applied Wicked state and rejects an out-of-range render scale before any
native call.

`RenderScene::set_shadow_quality` adds a checked Low/Medium/High preset for
Wicked's shadow atlas. The presets select 512/1024/2048-pixel 2D maps and a
quarter-resolution cube map; the native quality fixture checks the High state
and the C ABI rejects values outside the enum range. This keeps presentation
quality in Elisa while retaining a predictable renderer-side budget.

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` — render-scene API smoke and ordinary native maze smoke exited 0.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` — full shared suite exited 0; both Elisa Proof suites proved all 23 obligations with certificate replay.
- `/opt/homebrew/bin/python3 scripts/check_source_length.py`, `/opt/homebrew/bin/python3 scripts/check_module_hygiene.py`, and `git diff --check` passed.

This verifies setting changes reach Wicked's runtime state; image-difference
references, history-resource transition validation and measured GPU/VRAM costs
remain R07 work.
