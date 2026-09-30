# Whole-world save codec (W06)

`World::SaveGame` (src/world/world_save.elisa) serialises every live entity into a
versioned byte image: a 16-byte header (magic, version, count, FNV-1a32 checksum)
followed by 56-byte records (kind, parent index, gameplay payload, world transform).

- `save_world` writes into a caller buffer and raises `Capacity` when it is too small.
- `check_world_save` validates magic, version, length, checksum, kind codes, parent
  indices and transforms before anything is touched.
- `load_world` restores into an empty staging world (it refuses a populated one), so a
  rejected image never disturbs the live world. Hierarchy links go through
  `world_can_reparent`, so an image that encodes a cycle is rejected even when its
  checksum has been resealed.

test/world_save.elisa is gated. It covers the round trip (kinds, hierarchy, world
positions, gameplay), capacity, corruption, version, truncation, resealed cycles and
reload refusal. A negative control that skips the checksum comparison fails with 14.
Rehydrating native render/physics state from a loaded world is still open.

## Presentation rehydration

`WorldSaveRendering::rehydrate` (src/runtime/world_save_rendering.elisa) rebinds a
loaded world's presentation: every live entity gets its kind's visual and render ID
`base + live index`. Records keep live order, so the IDs match those of the saved world.
It refuses populated bindings, so rows from the replaced world cannot survive the swap.
test/world_save_rendering.elisa (gated) extracts render snapshots from the saved and
the loaded world and checks that IDs, meshes and poses match. A control that maps
guards to the structure visual fails with 12. Physics bodies are not yet rebuilt from a save.

## Physics rehydration

`WorldSavePhysics::rehydrate` (src/runtime/world_save_physics.elisa) creates a box body
per live entity of a loaded world, at its restored pose, from a per-kind body spec. It
refuses populated bindings and unknown kinds, and a failed creation destroys the bodies
this pass already made. The native smoke `world-save-physics-smoke`
(test/world_save_physics_native_main.elisa) loads a saved world under Jolt, checks
three bodies, the guard body's pose, refusal of a second pass, and that references from
the replaced world resolve to no body. A control without the empty-bindings guard fails
with status 16. Swapping a loaded world into a running game is still open.

## In-place swap

`World::SaveSwap::replace_world` (src/world/world_save_swap.elisa) swaps a save into the
running World. It first loads the payload into a throwaway staging world, so malformed
or cyclic saves are rejected before anything changes. It then captures a rollback
snapshot, despawns every live entity leaves-first, compacts, and respawns the saved
entities. The live world keeps its epoch and identity allocator, so the new IDs sit
above the old high-water mark and pre-swap references go stale instead of aliasing.
If the rebuild fails, the snapshot is restored. test/world_save_swap.elisa (gated)
covers corrupt-payload rejection with the live world intact, kind counts, the same
epoch, fresh IDs, stale old references and the preserved hierarchy. A control without
the staging check fails with 4. The rollback-after-failed-rebuild branch has no direct
test (staging makes it unreachable with current inputs). No shipped game drives
swap, then render and physics rehydration, end to end yet.

## End-to-end load

`WorldSaveLoad::load` (src/runtime/world_save_load.elisa) is the single load path for a
running game. It swaps the save into the live World, destroys the physics bodies of the
replaced entities, clears the render rows (new `WorldRendering::clear`), then rebuilds
render and physics for the loaded entities. A rejected save changes nothing. The native
smoke `world-save-physics-smoke` now also runs a live world with a bound crate through a
corrupt load (world and both binding tables untouched) and a good load (three entities,
three render rows starting at the chosen base, three bodies at the saved poses).
A control that skips destroying the old bodies fails with status 34. Old bodies are torn down with the new
`WorldPhysics::unbind_all`, so a body left bound to an already-despawned entity is also
destroyed (the smoke covers one); the course does not yet use this path.
