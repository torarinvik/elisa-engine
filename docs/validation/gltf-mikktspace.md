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
- The SDL3/Metal render-scene smoke now checks the textured panel's 16 cooked
  tangent frames after snapshot upload into Wicked. Every frame is finite,
  normalized, orthogonal to its uploaded normal, and has a valid handedness;
  the first known +X Elisa tangent arrives as -X in Wicked space. The same
  scene renders its authored cutout, lit, and emissive material strips.
- A separate mirrored-normal reference cooks two quads with opposite UV
  directions, verifies Wicked receives opposite tangent handedness on the two
  charts, and measures different lit-patch luminance in the captured frame.
  See [`mirrored-normal-map.md`](mirrored-normal-map.md).
- `/opt/homebrew/bin/python3 scripts/check_dependency_manifest.py` passed;
  MikkTSpace is pinned and remains outside the runtime link. The glTF helper
  builds only for offline cooking and caches the binary against source hashes
  and compiler versions.

## Boundaries

Authored glTF tangents retain their existing transform-and-handedness path.
Generated tangent splits remap positions, normals, UVs, morph deltas, and skin
influences together; cooked vertex limits are checked after splitting. For
lightmap UV generation, see [`gltf-lightmap-uv.md`](gltf-lightmap-uv.md).
Rendered lightmap quality and runtime lightmap authoring remain open.
