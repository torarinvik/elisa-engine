# Selecting FBX meshes during cooking

**Date:** 2026-09-23
**Scope:** Let callers cook one exact FBX mesh or mesh node by name while
preserving the existing largest-triangle-mesh default.

## Checks and results

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_fbx_import.py`
  passed. A deterministic two-mesh fixture checks exact mesh-name selection,
  exact node-name selection, rejection of a missing name, and rejection of an
  oversized selector.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_fbx_asset.py --self-test`
  passed. The native cooker selects the one-triangle mesh by name from a scene
  whose default largest mesh has two triangles; its simplification and
  deterministic-output checks also pass.

Use `scripts/cook_fbx_asset.py scene.fbx --asset-path assets/scene.fbx
--output build/selected.pkg --mesh-name MeshName` to select a mesh by its exact
mesh name or node name. Names are bounded to 512 UTF-8 bytes. Ambiguous matches
and names that match no triangle mesh fail explicitly.

## Boundaries

This cooks one selected mesh per package; it does not yet emit a complete
multi-mesh scene package, preserve FBX material bindings, or discover external
textures. The existing largest-mesh behavior remains the default for callers
that omit `--mesh-name`.
