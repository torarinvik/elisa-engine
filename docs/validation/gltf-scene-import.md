# Native glTF scene import

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

The bounded cgltf import and runtime cooker now carry a supported glTF scene
through normalized packages into Wicked resources. The cooker preserves node
placements, static and skinned mesh ranges, material subsets and textures,
cameras, punctual lights, inverse-bind palettes, helper-node rigs, morph
targets, and animation clips. Imported child resources are accessible through
generation-checked `RenderScene::imported_scene` handles; Wicked entities and
vendor types remain private. Snapshot creation, animation submission, and
destruction cover every placement, with rollback on failed creation.

The accepted profile is intentionally bounded: one selected scene, finite
triangle geometry, fixed vertex/index and hierarchy limits, at most 64 palette
bones, and embedded PNG/JPEG or supported Basis KTX2 images using the supported
UV/sampler profile. Unknown required extensions, Draco compression, unsupported
primitive modes, malformed references, unsupported texture transforms or UV
sets, and content beyond those bounds fail explicitly during cooking or load.
They are never silently omitted.

## Validation

- `scripts/gltf_hierarchy_self_test.py`, `scripts/gltf_skin_self_test.py`, and
  `scripts/gltf_morph_self_test.py` cover hierarchy transforms, independent
  skins, helper branches, morph streams, cubic tracks, and placement defaults.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py`
  passed the 104-case sanitized production-loader suite.
- The full `scripts/render_scene_native_smoke.py` passed on SDL3/Metal. It
  compiled the Elisa test client, rendered material and hierarchy fixtures,
  checked camera/light and multi-skin animation probes, then ran the maze app
  and packaged-maze checks outside the checkout. The packaged run rejected
  missing, escaping, corrupted, and dependency-invalid bundles, then passed
  with the valid bundle restored.
- The full `scripts/check.elisascript` suite passed, including Godot, module
  hygiene, source-length, proof replay, and release packaging gates.
- Commit `40147e3` fixed cancellation polarity in streaming SHA-256 reads; the
  async LOD chain now loads and is adopted by the native smoke. The focused LOD
  test covers both continued and cancelled hashing.

Evidence for the major slices: [node hierarchies](gltf-node-hierarchies.md),
[skin hierarchies](gltf-skin-hierarchies.md),
[snapshot placements](snapshot-mesh-placements.md),
[cooked textures](cooked-material-textures.md), and
[material subsets](gltf-material-subsets.md).
