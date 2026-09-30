# glTF CUBICSPLINE sampling (M06, ninth slice)

`GltfCubicSample` (`src/assets/gltf_cubic_sample.elisa`) evaluates CUBICSPLINE
channels. `GltfTrackSample::sample` now sends that mode to it instead of
raising `Unsupported`.

- Each key stores three entries: an in-tangent, a value and an out-tangent.
  `problem` now expects `3 × count × components` values in this mode.
- A segment is the cubic Hermite curve from the glTF specification
  (Appendix C), with tangents scaled by the segment duration.
- Outside the key times the first or last value is held. Rotations are
  normalized.

## Proof

`GltfSampleIndex::cubic_slot` is proved (`proof/gltf_sample_index.elisa`,
91/91 for the whole index, all replayed):

- slots are non-negative or -1;
- a part below 0 or above 2 is rejected.

## Tests

`test/assets_gltf_cubic_sample.elisa` checks:

- keys sampled from a cubic, with its exact derivatives as tangents,
  reproduce that cubic to 1e-12 at nine probe times. The segments have
  different lengths;
- the first key's in-tangent and the last key's out-tangent are ignored;
- end values are held outside the keys;
- a hand-computed vector midpoint;
- unit-length rotations;
- a single key;
- the three-entries-per-key width check.

Mutations of each Hermite basis term, the duration scaling, the swapped
in/out tangents, the held end, the rotation normalization, the slot layout
and the width rule all fail the tests. A redundant single-key clause turned
out to be an equivalent mutant and was removed.
