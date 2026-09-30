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
