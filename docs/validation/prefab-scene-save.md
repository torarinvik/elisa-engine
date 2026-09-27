# Nested prefab scene snapshots

`PrefabScene::capture` records the state of a live nested scene using stable scene, link, prefab, instance, and authoring IDs. Per-node local transforms and optional visual descriptors are copied as plain values. The snapshot contains no `World::EntityRef`, world epoch, renderer handle, or other process-local identity.

`PrefabScene::snapshot_valid` checks the saved scene ID, every link's stable identity, definition membership, per-prefab instance identity, node count, unique authoring IDs, finite transforms, and valid visual IDs and bounds before reconstruction starts. `PrefabScene::spawn_restored` then spawns the complete scene into a fresh `World`, applies saved per-node state in parent-first order, and destroys the spawned group if applying any record fails. Parent-linked root overrides are composed against their external scene parent so their saved local transform survives rebinding.

`test/prefab_scene.elisa` covers a nested placement override, a visual override, capture, rejection of an unknown authoring ID without allocating entities, and restore into a separately constructed world. It verifies that old references are invalid in the new world, the world-space placement and stable visual IDs survive, and all entities are released after teardown.

`PrefabSceneCodec` adds a bounded binary format for persisting this snapshot through
`UserData::write_payload` and `read_payload`. Version 1 uses a fixed little-endian
header, link rows, and override records; transforms retain their exact f32 bits,
and visual references contain stable asset IDs and bounds. The decoder rejects
unsupported versions, invalid IDs, malformed counts/flags, invalid transforms or
assets, truncation, and trailing bytes before returning a snapshot. Its payload
limit is 65,536 bytes, below the native byte-record limit. The UserData envelope
adds its own checksum and atomic replacement; runtime byte records still do not
use the fsynced journal and crash-recovery protocol from `scripts/save_journal.py`.

`test/prefab_scene_codec.elisa` covers a binary round-trip, float fidelity,
capacity preservation, truncation, wrong format versions, invalid flags, and
trailing data. The native application smoke saves and reloads a snapshot through
the public codec and native user-data service.

This persists one nested-scene snapshot; it does not yet encode a whole `World`,
reconstruct runtime scene definitions from `scripts/scene_file.py` JSON, or
rehydrate native mesh/material resources from stable IDs. Atomic snapshot writes
therefore do not yet amount to crash-recoverable whole-world saves.
