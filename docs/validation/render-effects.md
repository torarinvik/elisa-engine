# Render effects validation

`RenderSceneEffects` exposes Wicked's owned emitter and decal components through
value descriptors and generation checked scalar handles. Elisa callers never
receive a Wicked entity or component pointer.

The native adapter owns at most 32 live effects per scene through reusable
generation-checked slots. Exhaustion returns the typed `Capacity` error. Emitter creation
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

`WorldEffects` adds the first game-level ownership layer. It keeps the owner
entity and its emitter/decal handles in a bounded Elisa table, copies the
current World position into Wicked each update, scales emitter time per owner,
and destroys both native handles when an owner despawns. The same smoke moves
an attached entity, verifies the native transform, and then verifies despawn
cleanup returns the binding count to zero. Handle pools and native values stay
behind their owning modules; callers only hold the checked public descriptor
and handle types.

`WorldEffectAssets::Catalog` stores up to 32 validated authored emitter/decal
profiles under positive IDs. Profiles can be persistent or carry a finite
duration in scaled simulation seconds. A dispatched `Spawn` event uses its
payload as the profile ID; retrigger policy either rejects an active duplicate
or replaces it. `WorldEvents::emit_world` snapshots the entity position into
the event, so effect creation does not need to borrow the World while consuming
a queue phase. `WorldEffects::spawn_dispatched_events` attaches the native
handle to the event owner, applies emitter time scale, and leaves transform
following, timed expiry, and despawn cleanup to the existing bounded service.

The focused event smoke verifies emitter and decal profiles, duplicate IDs,
phase dispatch and per-owner position capture, the real Wicked emitter and
decal fields, fills the native effect slots and verifies capacity rejection,
releases and reuses a slot while rejecting its stale handle, and checks cleanup
after both owners despawn. It also checks native component counts after cleanup
and after a full RenderScene shutdown/reinitialize cycle. Repeated transient
Spawn events verify the replace policy; a scaled update expires both a decal
and an emitter and returns their native component counts to baseline. Run it
with the SDL3/Metal toolchain:

```text
ELISA_RENDER_SCENE_NATIVE_ONLY=1 \
ELISA_RENDER_SCENE_NATIVE_MAIN=test/render_scene_effect_events_native_main.elisa \
ELISA_COMPILER_BIN=../Elisa-compiler/bin/elisac-stage1 \
WICKED_ROOT=../amazing-labyrinth-wickedengine \
WICKED_BUILD=../amazing-labyrinth-wickedengine/build-elisa-sdl3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/render_scene_native_smoke.py
```

Native fixed-slot pooling, timed expiry, replace/reject retrigger policies, and
owner cleanup are covered. The remaining R09 work is a rendered combat or
environmental example and checking heap-memory baselines across restarts.
