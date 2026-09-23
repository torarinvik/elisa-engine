# Mesh LOD validation

`src/assets/lod.elisa` is the portable contract for cooked mesh LOD chains.
Each chain is bounded and keeps a stable material-subset identity while levels
are appended in nondecreasing screen-error order. Runtime selection is a small
metadata lookup; it does not own meshoptimizer state or allocate.

The contract rejects empty/invalid levels, duplicate mesh IDs, nonmonotonic
error thresholds, changed material subsets, and capacity overflow. Selection
returns the most detailed level that fits the requested screen-error budget.
The FBX cooker now tries a meshoptimizer vertex-cache reorder after optional
simplification and accepts it only when its measured cache miss ratio improves.
The native gate also simplifies an authored primitive to a bounded lower-index
LOD, reports the resulting geometric error, and keeps the original material
subset attached to the uploaded render mesh. Production cooked packages still
carry one mesh level; multi-LOD package emission and runtime screen-error
selection are not connected yet.

`test/asset_lod.elisa` covers deterministic fine/coarse selection, material
subset preservation, and rejection of invalid ordering and subset changes.

The glTF scene cooker now runs pinned meshoptimizer vertex-cache ordering over
each placement/material intersection. It preserves the triangle set and winding,
does not move triangles across placement or material boundaries, and leaves
vertex, skin, morph, tangent, and placement streams in their existing order.
The bounded native helper rejects malformed ranges and out-of-range indices.

Validation on 2026-09-23:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed. Its shuffled two-placement grid improved 16-entry ACMR from 2.8564 to 0.6602; repeated output was byte-identical.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed all 100 sanitized package-loader cases.
- Module hygiene, the 600-line source limit, and `git diff --check` passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py` exited 134 shortly after Wicked initialized and allocated a 256 MiB GPU buffer. It produced no render-group result, so visual runtime validation is still unverified.

This stage reorders indices only. Simplified LOD package emission, measured
visual error, and runtime screen-error selection remain open.
