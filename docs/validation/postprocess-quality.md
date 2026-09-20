# Post-process quality validation

`src/backend/quality.elisa` is the renderer-neutral profile contract. It keeps
quality selection in Elisa, validates render scale and bloom thresholds, and
offers bounded low/medium/high presets. `native/postprocess_bridge.h` translates
the contract to Wicked's `RenderPath3D` controls for tonemapping, bloom, FXAA,
SSAO, SSR, depth effects, and render scale. FSR1/FSR2 are enabled only when the
queried adapter says they are supported; otherwise the adapter returns an
explicit fallback after applying the rest of the profile.

Evidence: the shared Elisa gate runs `test/quality.elisa`; the SDL3/Wicked gate
calls `probe_postprocess_bridge` and checks both fallback and supported paths.
History-resource resizing and measured GPU/VRAM costs remain open R07 work.
