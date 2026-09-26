# Runtime renderer controls

`RenderScene` exposes checked runtime controls for indirect lighting, render scale, and adaptive resolution. `generate_static_lods` builds shared mesh LODs while preserving each material subset.

## Behavior

- `set_indirect_lighting` returns the mode the active device actually applied. Raster mode disables SSR, SSGI, and ray-traced GI/reflections. Screen-space mode requires the SSGI, deinterleave, and upsample shaders, then enables SSR and SSGI. Ray-traced mode requires Wicked's ray-tracing device capability and all diffuse/reflection shader stages. Unsupported ray tracing falls back to screen-space when its shaders are available, otherwise to raster.
- Mode changes disable the outgoing passes before creating resources for the incoming mode. Wicked shares its SSR target with ray-traced reflections, so the order avoids replacing a resource that is still in use.
- `set_render_scale` accepts scales from 0.5 through 1.0. `set_adaptive_resolution` accepts a target of 30–240 FPS, or zero to disable adaptation, and a minimum scale from 0.5 through 1.0. The controller samples CPU frame cadence over at least two seconds and 30 frames, ignores stalls over 100 ms, and changes scale in 0.0625 steps with a 3-second reduction and 6-second recovery cooldown. It is deliberately a frame-cadence controller, not a GPU timer.
- `generate_static_lods` accepts two to four total levels, a 0.1–0.8 triangle ratio, and a nonzero error bound up to 0.05. It rejects animated/shared-instance meshes, already-generated LODs, empty or malformed material subsets, invalid indices, non-finite positions, and index streams that would overflow Wicked's 32-bit subset offsets. LOD0 and its material assignments remain intact.

## Validation

Run the focused SDL3/Metal test on macOS:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" \
ELISA_RENDER_SCENE_CONTROLS_ONLY=1 \
/opt/homebrew/bin/python3.14 scripts/render_scene_native_smoke.py
```

The focused test checks invalid-value rejection, render-scale and adaptive settings, raster → screen-space → ray-traced → raster transitions against Wicked's live pass state, and three-level static LOD layout plus repeat-call rejection. It passed on macOS 27.0 / Apple M5 with Wicked HEAD `16ef79bd19b75ccbe39c3aab0eb16e0c216de6a5` after rebuilding `libWickedEngine.a` from the current checkout.

The Stage1 wrapper required `ELISA_ALLOW_STALE_STAGE1=1` because compiler sources changed after its generated product. The focused Elisa test source compiled successfully with that product. The full render-scene demo remains separately blocked by nine compiler backend declines in existing capability and maze functions; it was not used as evidence for this focused feature.
