# Selecting FBX meshes during cooking

**Date:** 2026-09-23
**Scope:** Let callers cook one exact FBX mesh or mesh node by name, or combine
all static triangle meshes into one package, while preserving the existing
largest-triangle-mesh default.

## Checks and results

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_fbx_import.py`
  passed. A deterministic two-mesh fixture checks exact mesh-name selection,
  exact node-name selection, rejection of a missing name, and rejection of an
  oversized selector.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_fbx_asset.py --self-test`
  passed. The native cooker selects the one-triangle mesh by name from a scene
  whose default largest mesh has two triangles; its simplification and
  deterministic-output checks also pass. `--all-meshes` combines the two
  source nodes into one 3-triangle geometry stream with two ordered subset
  ranges and records the source mesh count. Single-mesh packages report one
  source, and the package reader rejects zero or more than 1024 sources. The
  package also contains 80-byte placement records with source node IDs and
  post-cook vertex/index/subset ranges. Transforms are descriptive metadata
  because they have already been baked into vertices.
- The production reader's AddressSanitizer/UndefinedBehaviorSanitizer test loads
  the cooked two-mesh package and matches both placement records. The SDL3/Metal
  smoke runs this loader check on its freshly cooked FBX package.

Use `scripts/cook_fbx_asset.py scene.fbx --asset-path assets/scene.fbx
--output build/selected.pkg --mesh-name MeshName` to select a mesh by its exact
mesh name or node name. Names are bounded to 512 UTF-8 bytes. Ambiguous matches
and names that match no triangle mesh fail explicitly.

Use `scripts/cook_fbx_asset.py scene.fbx --asset-path assets/scene.fbx
--output build/scene.pkg --all-meshes` to combine every static triangle mesh.
Each node's geometry-to-world transform is baked into its positions. Material
slots and subset ranges stay distinct across source meshes, with a package-wide
limit of 16 slots and 256 placed source nodes (node indices below 256).
Placement records retain the FBX scene node index and world transform as
metadata. Vertex and index ranges are recalculated after simplification,
tangent seam splitting, and meshoptimizer remapping.

## Boundaries

All-mesh mode currently rejects skinned or animated scenes and bakes static
nodes into one geometry stream. Placement metadata preserves each source node's
identity and ranges, but the source hierarchy and local-space geometry are not
retained; placement transforms are descriptive because the same transforms are
already baked into positions. Use `--mesh-name` for a single skinned mesh. The
existing largest-mesh behavior remains the default when both selection options
are omitted.
