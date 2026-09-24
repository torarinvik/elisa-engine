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
packages are ready. `native/lod_manifest.h` parses this bounded versioned
schema and validates level order, relative package names, hashes, sizes, and
error budgets. `native/lod_geometry_chain.h` resolves every sibling under the
project root, verifies its exact SHA-256 and byte size, checks static geometry
counts, and requires stable placement, material-subset, texture, camera, and
light layouts across levels. Snapshot mesh registration and asynchronous
requests now accept `.lod.json`, retain all verified levels, and account for
their combined resident bytes. `native/lod_selection.h` selects the coarsest
level whose projected cumulative error fits a one-pixel budget, scaled by
`2^lod_bias`, with a 10% switching hysteresis. The RenderPath reevaluates
static snapshot rows against the active camera each frame and switches all
placements together through shared meshes. The native Metal regression invokes
the same camera-driven selection update and verifies the generated chain at
far, medium, and near distances: 288, 576, and 1,152 selected triangles.

Static simplification runs independently inside each placement/material
intersection, locks topological borders, and caps normalized geometric error
at 0.02. It compacts each placement's used vertices and remaps positions,
normals, UVs, tangents, and indices together. Animated, skinned, and morphed
geometry is rejected. Tiny ranges retain at least one triangle, so aggregate
triangle counts can slightly exceed a requested ratio.

The glTF and production FBX package cookers apply meshoptimizer v1.2's lossless
vertex codec to positions, normals, UV0, and optional float4 tangents, and its
index codec to triangle indices. Each stream uses the encoded representation
only when it is smaller; mixed raw and encoded streams are supported.
`meshopt_codec=meshoptimizer-v1.2` identifies the payload format. The bounded
native reader checks that tag, rejects duplicate, unknown, or corrupt codec
fields, restores the original little-endian streams, and continues to accept
earlier raw v2/v3 packages. FBX `.pkg` and textured `.elpk` cooks share the same
bounded package encoder. Lightmap UV1, rig, and morph streams remain raw.

Validation on 2026-09-23 and 2026-09-24:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed, including deterministic three-level chain generation, manifest bounds, repeated output, and package hashes. Its grid reduced 2,304 triangles to 576 across three variants.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed 104 sanitized geometry-loader cases, then ran the native LOD-chain test: all three packages passed SHA-256 and geometry/layout validation; malformed paths and schemas, a tampered package, and an escaping symlink were rejected under ASan/UBSan.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_glb_static_cook.py` passed: Blender and the production FBX cooker reduced 3,042 input triangles to 55 triangles and 37 vertices under the 64-triangle limit.
- `/opt/homebrew/bin/python3 scripts/check_module_hygiene.py`, `/opt/homebrew/bin/python3 scripts/check_dependency_manifest.py`, `/opt/homebrew/bin/python3 scripts/check_source_length.py`, Python syntax checks, and `git diff --check` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 ELISA_RUNTIME_OBJ="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/build/runtime/elisacore_runtime.o" PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript` passed on this tree, including Elisa tests, both proof replays, and reproducible release packaging.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 ELISA_RUNTIME_OBJ="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/build/runtime/elisacore_runtime.o" PYTHON_BIN=/opt/homebrew/bin/python3 /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` passed on 2026-09-23. It regenerated the three-level LOD fixture, passed the SDL3/Metal native tests, rendered the maze, and passed packaged execution with checkout access denied plus missing, escaping, corrupt, and invalid-dependency bundle checks.
- The live selector chose level 2 at camera distance 30, level 1 at 22.3, and level 0 at 21.5 for the 640x400 perspective test viewport. Each choice also matched the selected mesh's 288/576/1,152 triangle count. Visual quality review and GPU frame-time comparisons remain open; triangle counts measure submitted geometry, not GPU time.
- The native LOD visual regression renders the centered curved-grid fixture from the same camera pose at level 0 and the default one-pixel-budget level 2. It hides unrelated scene instances and screen-space overlays, waits for Wicked object pipelines, captures both presented frames, and verifies target visibility before comparison. The fixture winding faces the +Z camera. `scripts/compare_renders.py lod-quality` reported a 0.00276 mean maximum-channel error, 0.0196 p95, and 6.26% changed pixels above 2/255 in a 160x294 mesh crop; inspection showed the same rectangular silhouette and material region. Capture files are `build/render-scene-lod-fine.png` and `build/render-scene-lod-coarse.png`.
- On a deterministic 2,304-triangle, 1,250-vertex grid, meshoptimizer reduced the four core binary streams from 67,648 to 23,590 bytes (65.1%); the full-detail text package was 59,152 bytes. Its three-level chain contained packages of 59,152, 32,603, and 19,086 bytes and cooked in 3,628.9 ms with the native tool build cached. The smaller 12-triangle maze tile's core streams fell from 912 to 253 bytes. These are one-host cook and storage samples, not broad performance claims.
- `scripts/test_elisa_package.py` passed with compressed and legacy raw ELPK geometry. `scripts/test_geometry_subsets.py` passed 126 production-loader cases under ASan/UBSan, including exact valid compressed round-trips and rejected missing/unknown codec tags, duplicate stream fields, corrupt vertex/index payloads, and unknown compressed fields. `scripts/cook_gltf_asset.py --self-test` passed with deterministic codec output. `scripts/test_lod_manifest.py`, `scripts/test_fbx_import.py`, and `scripts/test_elisa_build_run.py` passed.
- The SDL3/Metal render-only smoke passed with codec sources linked into the application. The LOD visual crop retained the same silhouette and material region (mean maximum-channel error 0.00276, p95 0.0196). Five warmups plus 21 samples measured `RenderPath3D::Render()` GPU timestamp medians of 3,051 µs at level 0 and 3,084 µs at level 2 (p95 3,147 / 3,153 µs); completed-frame medians were 8,227 / 8,193 µs. This sample shows no LOD GPU-time improvement; repeat and non-Metal measurements remain open.
- On 2026-09-24, production FBX `.pkg` and textured `.elpk` output gained lossless core-stream and tangent encoding. The cooker self-test requires deterministic compressed bytes, a smaller stored stream total, the codec tag, and preserved vertex/triangle/index counts. A two-mesh fixture reduced all five geometry streams from 372 to 182 bytes (all five encoded); a textured cutout fixture reduced 312 to 196 bytes (three encoded). These are small fixture storage samples. The sanitized production loader accepts both outputs, including the FBX package without an explicit `indices=` count.
- Tangent compression validation: `DEVELOPER_DIR=/Library/Developer/CommandLineTools CXX=/opt/homebrew/opt/llvm/bin/clang++ /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed and confirms five compressed streams on tangent-bearing maze and textured-panel fixtures. `scripts/test_geometry_subsets.py` passed 127 cases, including a malformed encoded tangent rejection. The FBX self-test passed using matched Homebrew clang/clang++, and the generated two-mesh package plus textured ELPK passed together through the sanitized loader (129 cases, no failures). Raw/encoded fallback and codec-tag validation remain covered.
- The SDL3/Metal render-only smoke passed twice with compressed tangent packages loaded into Wicked. The first run measured LOD0/LOD2 GPU timestamp medians of 2,145/1,617 µs and completed-frame medians of 8,383/8,247 µs; its immediate repeat measured 3,054/2,979 µs and 8,070/8,020 µs. Both captures retained the same LOD image comparison (mean maximum-channel error 0.00276, p95 0.0196). The GPU medians varied substantially between runs, so these samples do not establish a repeatable LOD timing improvement; broader repeats and other hardware remain open.
