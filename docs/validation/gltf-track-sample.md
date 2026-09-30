# glTF track sampling (M06, fifth slice)

`GltfTrackSample` (`src/assets/gltf_track_sample.elisa`) evaluates one
animation channel at a given time.

- `GltfAnimationMap::sampler_bytes(..., times)` now maps either a sampler's
  output keys or its input key times. `channel_bytes` is the output case.
- `interpolation` reads a sampler's mode: LINEAR (the default), STEP or
  CUBICSPLINE, or -1 when the mode is unknown.
- `sample(times, values, components, path, mode, time)`:
  - finds the keys on either side of `time`, and clamps before the first
    key and after the last;
  - translation, scale and weights interpolate linearly;
  - rotation uses shortest-arc slerp, falling back to lerp for nearly
    parallel keys, then normalizes the result;
  - STEP holds the earlier key until the next key's time.
- `validate` / `problem` report `Unsupported` (CUBICSPLINE is not sampled
  yet), `Mismatch` (the number of values is not keys × width), `Unordered`
  (key times do not strictly increase) and `BadWidth` (rotation that is not
  a quaternion, or a width other than 1, 3 or 4).

## Proofs

`proof/gltf_sample_index.elisa`: 45/45 proven and replayed.

- `segment_start` lies in [0, count − 1] and leaves room for a second key.
- `segment_end` lies in [start, count − 1].
- `value_slot` is -1 or non-negative. Every read is also checked against
  the array length.

The interpolation arithmetic is float and is tested, not proved.

## Tests

`test/assets_gltf_track_sample.elisa`:

- The fixture's interpolation modes read as LINEAR and STEP. Its six key
  times load in increasing order.
- Samples of the rotation, translation and STEP scale channels match a
  Python evaluation of the same float32 keys to 1e-9. The samples cover
  between keys, at a key, before the first key and after the last.
- As an independent check, slerp about one axis turns the angle linearly
  to within 1e-7. The keys are unit length only to float32 precision,
  which bends it by about 1e-9.
- Shortest arc: a negated end key gives the half-angle rotation.
- STEP versus LINEAR on a scalar track, and each typed error.

Mutation check: removing the shortest-arc flip, the STEP hold or the slerp
weights makes the test fail (exits 18, 15 and 10). Changing `<=` to `<` in
the key search does not: at an exact key time both neighbouring segments
give the same value, so that mutant is equivalent.

## Open

- CUBICSPLINE sampling.
- Composing local and world poses. This needs decimal parsing of the
  nodes' rest translation, rotation and scale.
- The FBX path.
- The export CLI.
