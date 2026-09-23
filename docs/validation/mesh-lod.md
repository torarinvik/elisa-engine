# Mesh LOD validation

`src/assets/lod.elisa` is the portable contract for cooked mesh LOD chains.
Each chain is bounded and keeps a stable material-subset identity while levels
are appended in nondecreasing screen-error order. Runtime selection is a small
metadata lookup; it does not own meshoptimizer state or allocate.

The contract uses the engine's full 128-bit `Assets::AssetId` for both mesh and
material-subset identity. It rejects invalid IDs, non-live or empty levels,
non-finite/negative errors, duplicate mesh IDs, nonmonotonic thresholds,
changed material subsets, and capacity overflow. Selection returns the most
detailed level that fits a finite, nonnegative screen-error budget. Private
validation helpers keep the checks inside the module boundary.
The FBX cooker now tries a meshoptimizer vertex-cache reorder after optional
simplification and accepts it only when its measured cache miss ratio improves.
The native gate also simplifies an authored primitive to a bounded lower-index
LOD, reports the resulting geometric error, and keeps the original material
subset attached to the uploaded render mesh. Production cooked packages still
carry one mesh level; multi-LOD package emission and runtime screen-error
selection are not connected yet.

`test/asset_lod.elisa` covers fine/coarse selection, material-subset
preservation, ordering errors, invalid levels and budgets, NaN rejection, and
full-width asset identity (including distinct IDs with the same low word).

The glTF scene cooker runs pinned meshoptimizer vertex-cache ordering over
each placement/material intersection. It preserves the triangle set and winding,
does not move triangles across placement or material boundaries, and leaves
vertex, skin, morph, tangent, and placement streams in their existing order.
The bounded native helper rejects malformed ranges and out-of-range indices.

Static glTF meshes can now be cooked as reduced variants with
`--simplify-ratio 0.5`. The pinned meshoptimizer simplifier runs independently
inside each placement/material range, locks topological borders, and caps its
relative geometric error at 0.02 of that range's extent. The cooker then
compacts each placement's used vertices independently and remaps indices,
positions, normals, UVs, and tangents together. It rewrites placement vertex and
index ranges while retaining material-subset boundaries. It rejects overlapping
placement vertex ranges and references outside a placement's source range.
Skin, animation, and morph meshes are rejected. A ratio is a per-range target;
tiny ranges retain at least one triangle, so the aggregate may exceed the
requested fraction slightly. The command reports triangle and vertex counts,
attribute-stream bytes, and maximum measured simplification error.

The test cooker emits a two-placement, two-material grid package through the
production CLI. Each of its four placement/material ranges is reduced while
retaining material order. The production C++ package loader accepts the result
and tests verify each remapped index stays inside its compacted placement. On
the deterministic single-placement fixture, 1,152 triangles become 576,
625 vertices become 337, and the position/normal/UV/tangent streams shrink from
30,000 to 16,176 bytes. This is still one package per cook; it does not yet emit
a LOD chain, attach screen-error metadata, or select levels at runtime.

Validation on 2026-09-23:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed. Its shuffled two-placement grid improved 16-entry ACMR from 2.8564 to 0.6602; repeated output was byte-identical.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 -c 'import sys; sys.path.insert(0, "scripts"); import cook_gltf_lod; raise SystemExit(cook_gltf_lod.self_test())'` passed: 1,152 -> 576 triangles, 625 -> 337 vertices, 30,000 -> 16,176 attribute bytes; material subsets and malformed packets were checked.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed all 101 sanitized package-loader cases, including compacted streams and per-placement remapped indices in the reduced two-placement glTF package.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed. `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_glb_static_cook.py` passed: the bounded GLB pipeline cooked 3,042 source triangles into 55 triangles and 37 vertices.
- `python3 scripts/check_module_hygiene.py`, `python3 scripts/check_dependency_manifest.py`, `/opt/homebrew/bin/python3 scripts/check_source_length.py`, and `git diff --check` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` exited 134 shortly after Wicked initialized and allocated a 256 MiB GPU buffer. It produced no render-group result, so visual runtime validation is still unverified.

Visual quality review and performance comparison remain open. Reduced static
variants are available as separate files, but automatic multi-LOD packaging
and runtime screen-error selection remain open.

Validation on 2026-09-23:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/asset-lod-test test/asset_lod.elisa && build/asset-lod-test` passed. The explicit stale-stage1 allowance was needed because the seeded compiler binary is newer than compiler source; the compiler's self-host gate currently reports nullable-flow diagnostics in its own sources.
- `python3 scripts/check_module_hygiene.py`, `/opt/homebrew/bin/python3 scripts/check_source_length.py`, and `python3 scripts/check_dependency_manifest.py` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_ALLOW_STALE_STAGE1=1 ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 ELISA_RUNTIME_OBJ="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/build/runtime/elisacore_runtime.o" PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript` passed the complete shared gate, both proof replays, and reproducible release packaging. The selected compiler entry is the installed stage1 wrapper, which supplies the environment required by `-emit c-archive`; the raw product binary returned an empty diagnostic and exit 2 for that packaging step. The world test binding is now explicitly mutable for its `world_compact` call.
- The focused render smoke remains unverified because the previous SDL3/Metal run exited 134 during Wicked initialization, before reporting render groups.
