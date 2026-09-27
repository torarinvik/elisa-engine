# Nested prefab scene snapshots

`PrefabScene::capture` records the state of a live nested scene using stable scene, link, prefab, instance, and authoring IDs. Per-node local transforms and optional visual descriptors are copied as plain values. The snapshot contains no `World::EntityRef`, world epoch, renderer handle, or other process-local identity.

`PrefabScene::snapshot_valid` checks the saved scene ID, every link's stable identity, definition membership, per-prefab instance identity, node count, unique authoring IDs, finite transforms, and valid visual IDs and bounds before reconstruction starts. `PrefabScene::spawn_restored` then spawns the complete scene into a fresh `World`, applies saved per-node state in parent-first order, and destroys the spawned group if applying any record fails. Parent-linked root overrides are composed against their external scene parent so their saved local transform survives rebinding.

`test/prefab_scene.elisa` covers a nested placement override, a visual override, capture, rejection of an unknown authoring ID without allocating entities, and restore into a separately constructed world. It verifies that old references are invalid in the new world, the world-space placement and stable visual IDs survive, and all entities are released after teardown.

This is the in-memory Elisa reconstruction boundary. `scripts/scene_file.py` and `scripts/save_journal.py` already validate and durably store stable scene definitions, links, and overrides, but no runtime JSON decoder currently converts a loaded file into `PrefabScene::Scene` and `SceneSnapshot`. Native mesh/material resources also still need to be registered or restored from their stable IDs.
