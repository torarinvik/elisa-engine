# FBX polygon material subsets

**Date:** 2026-09-23  
**Scope:** Preserve FBX polygon material assignments and supported, untextured PBR factors in Elisa's existing cooked geometry and snapshot material paths.

## Behavior

The bounded ufbx importer reads each face's material slot, groups triangles by slot, and emits an ordered subset partition. It accepts at most 16 slots and rejects invalid face assignments. Tangent generation and vertex-fetch remapping preserve the partition. Vertex-cache optimization runs per subset, and static-mesh simplification allocates its triangle budget across subsets and simplifies each slot independently; the budget must retain at least one triangle per non-empty subset. Skin simplification remains rejected.

The cooker writes the existing subset fields plus each authored slot's 48-byte factor record and a length-prefixed UTF-8 material name. The importer maps diffuse/base color and factor, metallic, roughness (including ufbx's Phong conversion), emissive color and factor, transparency to alpha/blend mode, and the double-sided feature. The production geometry reader validates factor bounds, name count, UTF-8, NULs, and the 256-byte name limit. RenderScene's existing `register_snapshot_mesh_material` and `register_snapshot_mesh_material_set` APIs turn cooked factors into ordinary snapshot materials; applications still choose material IDs and bind the set to rows.

External textures are rejected with a clear import error until FBX texture paths can be resolved and images safely cooked into the package. The importer does not yet create a complete multi-mesh scene package, infer alpha-mask policy, or expose slot names through the RenderScene C ABI. Material-factor coverage currently uses deterministic Phong fixtures; other FBX material dialects need representative parity fixtures before the importer can claim broad material equivalence.

## Validation

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_fbx_import.py` passed. Its deterministic FBX fixture assigns two polygons to distinct materials and checks exact subset ranges, names, normalized factors, blend alpha, and double-sided policy.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_fbx_asset.py --self-test` passed. It validates the two-slot package's names and factor records, rejects a one-triangle budget for two subsets, and simplifies a planar 128-triangle, two-material grid to 64 triangles while preserving both exact index partitions.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed: the sanitized production loader accepted the valid material names and rejected truncated, overlong, NUL-containing, malformed UTF-8, and trailing-byte name streams.
- The SDL3/Metal RenderScene smoke loads the cooked FBX package, registers its two cooked materials through the ordinary snapshot-material API, checks the imported PBR/blend factors in Wicked, and checks that both geometry subsets resolve to their expected slot materials. The full `scripts/render_scene_native_smoke.py` gate passed, including execution of the packaged maze outside the checkout.
