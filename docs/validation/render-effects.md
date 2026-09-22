# Render effects validation

`RenderSceneEffects` exposes Wicked's owned emitter and decal components through
value descriptors and generation checked scalar handles. Elisa callers never
receive a Wicked entity or component pointer.

The native adapter owns at most 32 live effects per scene. Emitter creation
validates the particle limit, count, lifetime, size, position, and bounded
velocity before calling `EmittedParticleSystem::SetMaxParticleCount`. Decal
creation and updates validate finite colors, positive range, and nonnegative
slope blending. A stale or foreign handle is rejected at the ABI boundary, and
the typed Elisa API prevents mixing emitter and decal handles.

The native smoke creates an emitter and decal, verifies the actual Wicked
component fields, advances the emitter, updates the decal, rejects malformed
values, and checks destruction plus stale-handle rejection. It runs with the
SDL3/Metal gate:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/Documents/Coding\ Projects/Elisa\ Projects/Elisa-compiler/scripts/elisac_stage1.sh \
python3 scripts/render_scene_native_smoke.py
```

Game-level effect spawning, owner attachment, time scaling, and authored
emitter/decal assets remain higher-level work.
