# Prefab scene cooked-resource restore

`PrefabSceneCodec` preserves stable mesh and material IDs in its saved scene
snapshot. `AssetResourceCatalogue` now maps those IDs to cooked package and
section names; `PrefabSceneResources::build_plan` validates the snapshot,
resolves its visual IDs, and deduplicates repeated references before any
stream request starts. The bounded plan supports up to 1,024 distinct IDs,
which covers the current limit of 8 links, 64 authoring records per link, and
two IDs per visual.

`PrefabSceneResources::request` queues every planned package section through
`AssetStream`. If a request fails, it drops handles acquired by that call. A
failed rollback remains owned by the `Batch` and can be retried with
`release`; out-of-range accessors return empty sentinels. An empty valid plan
queues no work. Resource locations are borrowed C strings, so their backing
storage must outlive lookups and requests. Callers still populate the runtime
catalogue from their cooked/package metadata; the runtime does not read the
editor SQLite catalogue.

`test/prefab_scene.elisa` uses a nested scene snapshot with repeated visual
IDs. It checks that the resource plan emits one mesh and one material entry,
resolves both package sections, and leaves the output plan empty when a
snapshot ID lacks a catalogue mapping. `proof/prefab_scene_resources.elisa`
proves the scalar prefix guards used by the production resource-ID scan.

The Character Course still has its small authored ID-to-package table in
`examples/character_course/beacons.elisa`; this API provides the reusable
catalogue and request layer for moving such mappings into runtime package
metadata. Package requests themselves require an initialized native
application, so the portable scene test covers planning and validation rather
than native stream pumping.
