# Render effects validation

`RenderSceneEffects` exposes Wicked's owned emitter and decal components through
value descriptors and generation checked scalar handles. Elisa callers never
receive a Wicked entity or component pointer.

The native adapter owns at most 32 live effects per scene through reusable
generation-checked slots. Exhaustion returns the typed `Capacity` error. Emitter creation
validates the particle limit, count, lifetime, size, position, and bounded
velocity before calling `EmittedParticleSystem::SetMaxParticleCount`. Decal
creation and updates validate finite colors, a finite nonzero quaternion,
positive range, and nonnegative slope blending. The bridge normalizes decal
rotation, sets Wicked's transform scale from the requested range, and keeps the
Wicked material color synchronized with the decal component. A stale or foreign
handle is rejected at the ABI boundary, and the typed Elisa API prevents mixing
emitter and decal handles.

The native smoke creates an emitter and decal, verifies the actual Wicked
component fields (including decal rotation and projected range), advances the
emitter, updates the decal, rejects malformed values, and checks destruction
plus stale-handle rejection. It runs with the SDL3/Metal gate:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
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
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
WICKED_ROOT=../amazing-labyrinth-wickedengine \
WICKED_BUILD=../amazing-labyrinth-wickedengine/build-elisa-sdl3 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
python3 scripts/render_scene_native_smoke.py
```

Native fixed-slot pooling, timed expiry, replace/reject retrigger policies, and
owner cleanup are covered.

`examples/environmental_effects` is an authored SDL3/Wicked environment vignette
that routes two typed Spawn events through `WorldEvents`, a validated effect
catalog, and `WorldEffects`. The owner is a normal Elisa World entity; the
native emitter follows and ticks with it, and the decal remains until owner
cleanup. A few Elisa-authored ember and scorch meshes make the event's visual
response stable to compare across device pipelines while the native components
exercise their production lifecycle. The hidden render smoke waits for the
scene pipelines, captures a baseline, dispatches the event, captures the impact,
and verifies a visible image difference before cleanup:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
WICKED_ROOT=../amazing-labyrinth-wickedengine \
WICKED_BUILD=../amazing-labyrinth-wickedengine/build-elisa-sdl3 \
python3 scripts/environmental_effects_smoke.py
```

On macOS 27.0 / Apple M5 the latest run changed 8,084 pixels in the 2,304,000-
pixel capture. The separate native event smoke still checks emitter/decal
component counts before and after a RenderScene restart.

Render smoke group 238 (`test/render_scene_effect_memory_native.elisa`) checks
memory as well as counts. Each cycle attaches an emitter and a decal to a World
owner through `WorldEffects`, ticks four updates, despawns the owner, and checks
that the binding count and the Wicked emitter/decal counts are zero. It then
shuts the RenderScene down and initializes it again, and checks the counts again.
After one warm-up cycle, the test hook `elisa_render_scene_v1_test_footprint_kib`
reads Mach `TASK_VM_INFO` `phys_footprint`, which includes Metal allocations
charged to the process. Six measured cycles must stay within 8 MiB of the
post-warm-up sample. On 2026-10-02 (macOS 27.0, Apple M5), the post-warm-up
sample was 1,149,921 KiB and all six cycles read 1,149,873 KiB, so memory did
not grow. Setting the tolerance to -1 makes the smoke exit 238, as expected.
