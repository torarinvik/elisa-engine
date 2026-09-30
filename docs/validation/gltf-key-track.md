# glTF key tracks (M06, third slice)

`GltfKeyTrack` (`src/assets/gltf_key_track.elisa`) turns a channel mapped
by `GltfAnimationMap` into f64 key values and writes edited keys back.

- `load` reads the channel's BIN range and decodes every binary32 key.
  Channels that are not float (`NotFloat`), ranges outside the BIN chunk
  (`OutsideBin`) and NaN or infinite keys (`NonFinite`) are rejected.
- `value_at(track, key, component)` reads one component. Keys or components
  outside the channel fail with `OutsideKeys`.
- `store(bytes, layout, channel, key, component, value)` rounds `value` to
  the nearest binary32 (ties to even), patches exactly that key's four bytes
  and returns the stored value. NaN fails with `NonFinite` and values past
  the largest float with `Overflow`.

Elisa has no bit-cast, so the conversion is exact power-of-two arithmetic
on the sign, exponent and significand fields. It covers subnormals, the
sign of zero, and the carry when rounding reaches 2^24.

## Proofs

`proof/gltf_key_index.elisa`: 56/56 proven and replayed.

- `slot_offset`: a slot's four bytes lie inside the channel's byte length,
  and the offset is exactly `index * 4`.
- `slot`: a key/component slot is -1 or non-negative. Its upper bound
  timed out in the prover. It is not needed for safety because every write
  also goes through `slot_offset` and `GlbContainer::patch_bin`, which are
  both bounded.
- `finite_field`: only exponent fields 0–254 are accepted.
- `carried_field`: a rounding carry keeps the exponent field within 1–255,
  and 255 is then rejected as overflow.

The float arithmetic itself is not proved, because the prover has no f64
support. It is tested instead.

## Tests

`test/assets_gltf_key_track.elisa`:

- known patterns: 1.0, −2.5, the smallest subnormal, the largest float,
  infinity and NaN, 0.1 and ±0;
- round trips of edge patterns at the subnormal/normal boundary and at the
  sign bit;
- ties to even at 1 + 2^-24 and 1 + 3·2^-24, and at the half-subnormal;
- the carry at 2 − 2^-25, and overflow at ±3.5e38;
- all six channels of the Blender fixture, which includes a −0.0 key, load
  and re-encode to their exact bytes;
- editing one rotation key changes only its four bytes and reloads as the
  new value;
- a non-float value is stored rounded;
- out-of-range keys, NaN and overflow give their typed errors;
- every channel still round-trips after the edits.

Mutation check: breaking the tie rule, the carry, negation or the
subnormal scale makes the test fail (exits 10, 14, 2 and 8).

## Open

- Assembling tracks per bone into an editable skeleton (node hierarchy).
- The FBX path.
- An export CLI that writes the edited image to disk.
