# FBX polygon material subsets

**Date:** 2026-09-23  
**Scope:** Preserve per-polygon FBX material-slot assignments in normalized
cooked geometry, including when unskinned meshes are simplified.

## Checks and results

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_fbx_import.py`
  passed. The synthetic two-material FBX imports as two ordered, contiguous
  index subsets with material slots 0 and 1.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_fbx_asset.py --self-test`
  passed. The cooker emits and validates the two material subsets, rejects a
  triangle budget smaller than the number of subsets, and simplifies a 512-
  triangle two-material grid to 128 triangles while preserving both subset
  ranges. The one-material deterministic simplification and mesh optimization
  checks also pass.
- The full SDL3/Metal native render-scene smoke passed after the cooker began
  emitting subset metadata, including package loading, actual Wicked rendering,
  and packaged-maze runs with checkout access denied.
- Module hygiene, dependency-manifest, source-length, Python compilation, and
  `git diff --check` gates passed.

A cooked mesh has at most 16 material slots and 16 non-empty subsets. Each subset
is an exact ordered partition of the index stream. Simplification allocates at
least one triangle to every source subset, then simplifies each independently;
vertex-cache optimization never crosses subset boundaries.

## Boundaries

The package preserves which polygons refer to which slot, but this step does not
extract FBX material factors, connect external textures, or cook all meshes in a
scene. Callers still provide resolved materials for the package's slots.
