# Mesh LOD validation

`src/assets/lod.elisa` owns the portable bounded LOD-chain contract. It keeps
stable mesh and material-subset identities, validates nondecreasing screen-error
thresholds, and selects the most detailed live level that fits a pixel-error
budget. `projected_error_pixels` converts meshoptimizer's normalized geometric
error using the full-detail asset extent, the maximum world scale, and the
camera's projected pixels per world unit at the nearest relevant depth. Using
the full asset extent bounds every simplified subset, so the result can be
conservative for scenes whose subsets differ greatly in size.

`scripts/cook_gltf_asset.py --lod-ratios 0.5,0.25` cooks a full-detail package
plus each strictly descending simplification ratio. For
`--output assets/prop.pkg`, it writes content-addressed sibling packages and
`assets/prop.lod.json`. The manifest records source/package SHA-256 values,
package byte sizes, triangle/vertex counts, attribute bytes, the source-space
extent, and meshoptimizer's normalized geometric error with a cumulative
monotonic budget. The manifest is published atomically after all referenced
packages are ready. `native/lod_manifest.h` now parses this bounded versioned
schema and validates level order, relative package names, hashes, sizes, and
error budgets. The sanitized production-loader suite reads the generated
manifest and loads every referenced package. `RenderScene` does not yet consume
the manifest or switch levels at runtime.

Static simplification runs independently inside each placement/material
intersection, locks topological borders, and caps normalized geometric error
at 0.02. It compacts each placement's used vertices and remaps positions,
normals, UVs, tangents, and indices together. Animated, skinned, and morphed
geometry is rejected. Tiny ranges retain at least one triangle, so aggregate
triangle counts can slightly exceed a requested ratio.

Validation on 2026-09-23:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed, including deterministic three-level chain generation, manifest bounds, repeated output, and package hashes. Its grid reduced 2,304 triangles to 576 across three variants.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed 104 sanitized geometry-loader cases, then ran the native LOD-manifest test: all three referenced packages loaded and five malformed manifests were rejected under ASan/UBSan.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_glb_static_cook.py` passed: Blender and the production FBX cooker reduced 3,042 input triangles to 55 triangles and 37 vertices under the 64-triangle limit.
- `python3 scripts/check_module_hygiene.py`, `python3 scripts/check_dependency_manifest.py`, `/opt/homebrew/bin/python3 scripts/check_source_length.py`, and `git diff --check` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 ELISA_RUNTIME_OBJ="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/build/runtime/elisacore_runtime.o" PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript` passed on this tree, including Elisa tests, both proof replays, and reproducible release packaging.
- The focused SDL3/Metal `scripts/render_scene_native_smoke.py` previously exited 134 during Wicked initialization, before reporting render groups. Visual LOD selection and performance measurements remain unverified.
