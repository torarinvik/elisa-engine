# glTF MikkTSpace cooking

**Date:** 2026-09-23
**Scope:** Generate glTF tangent frames with pinned MikkTSpace during offline
cooking, and retain vertex-aligned runtime streams when tangent seams split.

## Checks and results

- `/opt/homebrew/bin/python3 scripts/cook_gltf_mikktspace.py` passed. The
  mirrored-UV quad splits from four input vertices to six cooked vertices,
  preserves opposite handedness across the shared edge, and repeats byte for
  byte. A degenerate UV chart receives finite fallback frames.
- `/opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed.
  Its integrated textured-panel fixture verifies opposite handedness on the
  two sides of a mirrored seam, stable remapped positions/normals/UVs, and
  matching morph deltas for duplicated vertices. The skinned fixture verifies
  that seam-split vertices retain their exact joint indices and weights.
- `/opt/homebrew/bin/python3 scripts/check_dependency_manifest.py` passed;
  MikkTSpace is pinned and remains outside the runtime link. The glTF helper
  builds only for offline cooking and caches the binary against source hashes
  and compiler versions.

## Boundaries

Authored glTF tangents retain their existing transform-and-handedness path.
Generated tangent splits remap positions, normals, UVs, morph deltas, and skin
influences together; cooked vertex limits are checked after splitting. For
lightmap UV generation, see [`gltf-lightmap-uv.md`](gltf-lightmap-uv.md).
Mirrored-normal-map render captures remain open.
