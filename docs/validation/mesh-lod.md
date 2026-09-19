# Mesh LOD validation

`src/assets/lod.elisa` is the portable contract for cooked mesh LOD chains.
Each chain is bounded and keeps a stable material-subset identity while levels
are appended in nondecreasing screen-error order. Runtime selection is a small
metadata lookup; it does not own meshoptimizer state or allocate.

The contract rejects empty/invalid levels, duplicate mesh IDs, nonmonotonic
error thresholds, changed material subsets, and capacity overflow. Selection
returns the most detailed level that fits the requested screen-error budget.
The native cook stage can populate this contract with optimized, simplified,
compressed meshes and optional meshlets once the active Wicked path consumes
those artifacts.

`test/asset_lod.elisa` covers deterministic fine/coarse selection, material
subset preservation, and rejection of invalid ordering and subset changes.
