# FBX polygon material subsets

**Date:** 2026-09-23  
**Scope:** Preserve polygon material assignments as cooked index subsets so FBX meshes use the existing snapshot material-set path.

## Behavior

The bounded ufbx importer reads each face's material slot, groups triangles by slot, and emits an ordered subset partition. It accepts at most 16 slots and rejects invalid face assignments. Tangent generation and vertex-fetch remapping preserve the partition. Vertex-cache optimization runs per subset, and static-mesh simplification allocates its triangle budget across subsets and simplifies each slot independently; the budget must retain at least one triangle per non-empty subset. Skin simplification remains rejected.

The cooker writes the existing `material_slots`, `subset_count`, `subset_stride=12`, and `subsets_b64` package fields. The production package reader already validates this format, and snapshot rows can bind the imported mesh through the regular `SnapshotMaterialSet` API. Slot factors, names, textures, and automatic FBX material registration are not included yet; applications provide materials in the source node's slot order.

## Validation

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_fbx_import.py` passed. Its deterministic FBX fixture assigns two polygons to distinct materials and checks exact subset ranges and slot indices.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_fbx_asset.py --self-test` passed. It validates the two-slot cooked package, rejects a one-triangle budget for two subsets, and simplifies a planar 128-triangle, two-material grid to 64 triangles while preserving both exact index partitions.
- The SDL3/Metal RenderScene subset smoke loads a cooked FBX package, assigns a two-entry material set, and checks that each Wicked subset resolves to the expected slot material. The full `scripts/render_scene_native_smoke.py` gate passed with this case included.
