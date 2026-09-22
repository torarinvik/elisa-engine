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
