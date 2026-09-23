# glTF lightmap UV cooking

**Date:** 2026-09-23
**Scope:** Preserve authored glTF `TEXCOORD_1` or generate a bounded,
deterministic xatlas atlas during offline cooking, then retain the vertex-aligned
stream through cooked packages and Wicked mesh upload.

## Checks and results

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_lightmap_uv.py`
  passed. Repeated xatlas runs produce identical UVs, source-vertex mappings,
  and indices. Fixtures cover authored UV1 through MikkTSpace seam splits,
  generated UV1 on a multi-material mesh, skin weights, morph deltas, and
  simplified placement ranges.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test`
  passed, including validation that atlas options require generation and stay
  within the configured resolution and padding limits.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py`
  passed under AddressSanitizer and UndefinedBehaviorSanitizer. The production
  loader accepts authored and generated UV1, including a skinned xatlas package
  with inverse-bind matrices, and rejects malformed streams, unsupported
  generator revisions, invalid metadata, and out-of-range atlas coordinates.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 ELISA_RUNTIME_OBJ="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/build/runtime/elisacore_runtime.o" PYTHON_BIN=/opt/homebrew/bin/python3 /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py`
  passed on SDL3/Metal. It cooks a generated-UV1 package, loads it into Wicked,
  and checks that the uploaded second UV stream matches the mesh vertex count
  and contains finite normalized coordinates.

The native xatlas wrapper is built and cached only for offline cooking. The
package records the pinned xatlas revision, atlas resolution, padding, and
chart count. Resolution is bounded to 16–8192 pixels and padding to 0–64
pixels; generated coordinates are checked to lie in `[0, 1]`. Atlas generation
runs after geometry simplification and remaps every vertex-indexed stream
together, including morph targets, skin influences, and placement ranges.

## Boundaries

This adds UV generation and GPU stream transport. It does not add lightmap
material authoring, baking, sampling, or a lightmap render pass. Visual chart
quality and mirrored-normal-map reference captures still need review. Runtime
loading accepts authored UV1 without xatlas generation metadata and accepts
generated UV1 only when its exact pinned generator revision and bounded
settings are present.
