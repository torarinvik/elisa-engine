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
  ranges and records the source mesh count.

Use `scripts/cook_fbx_asset.py scene.fbx --asset-path assets/scene.fbx
--output build/selected.pkg --mesh-name MeshName` to select a mesh by its exact
mesh name or node name. Names are bounded to 512 UTF-8 bytes. Ambiguous matches
and names that match no triangle mesh fail explicitly.

Use `scripts/cook_fbx_asset.py scene.fbx --asset-path assets/scene.fbx
--output build/scene.pkg --all-meshes` to combine every static triangle mesh.
Each node's geometry-to-world transform is baked into its positions. Material
slots and subset ranges stay distinct across source meshes, with a package-wide
limit of 16 slots.

## Boundaries

All-mesh mode currently rejects skinned or animated scenes and combines static
nodes into one render mesh; it does not preserve separate node identities,
hierarchy, or per-node runtime transforms. Use `--mesh-name` for a single
skinned mesh. The existing largest-mesh behavior remains the default when both
selection options are omitted.
