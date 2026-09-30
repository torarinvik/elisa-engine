# glTF animation channel map (M06, second slice)

`GltfJson` (`src/assets/gltf_json.elisa`) reads the GLB JSON chunk in one
pass with a byte-level state machine. It records one node per value: kind,
byte range, parent, and the member key's byte range. Strings are not copied
or decoded. Non-negative integers up to 2^32 − 1 are decoded and every
other number is kept as a byte range. Malformed text fails with a typed
`JsonError`:

- `Malformed`: bad syntax, a trailing comma, a control byte in a string,
  or a second root value;
- `Truncated`: the input ends inside a value;
- `TooDeep`: more than 64 levels of nesting;
- `TooManyNodes`: more than 10^6 values.

`GltfAnimationMap::channel_bytes` follows a channel through its sampler,
output accessor and buffer view to the BIN range that holds the channel's
keys. It also returns the target node, the path, the key count and the
component layout. Anything the editor must not guess at fails with a typed
`MapError`: sparse accessors, strided views, buffers other than the GLB's
own, and missing or inconsistent indices. With `GlbContainer::patch_bin`,
an edit to one bone rewrites only that channel's accessor bytes.

## Evidence

- `tools/gltf_channel_fixture.py` has Blender's glTF exporter write a
  two-bone armature with a 6-frame take (2,660 bytes, 6 channels). It
  embeds the file in `test/assets_gltf_channel_fixture.elisa`, together
  with each channel's target, path and byte range as read by Python's
  `json` module.
- `test/assets_gltf_animation_map.elisa` (in the gate) checks:
  - every channel's mapping against those values;
  - that missing animations and channels fail with a typed error;
  - that patching one channel changes only bytes inside its range, and the
    edited file maps to the same ranges again;
  - 13 JSON syntax cases: nesting depth 64 is accepted and 65 rejected,
    and 2^32 parses as a real number rather than overflowing.

  Mutating an expected value, a JSON verdict or the edit-range check each
  makes the test fail (exits 101, 10 and 5).
- `proof/gltf_json_index.elisa` has 81/81 obligations proven, all replayed.
  - Integer digits never exceed 2^32 − 1 and fail as −1 instead.
  - An accepted depth lies in 0..64.
  - Component sizes lie in 0..4.
  - An accessor's offset inside its view is accepted only when its span
    fits the view.

  As with the GLB container, the converse (every fitting accessor is
  accepted) timed out in the prover and is only tested.

## Open

- Loading the mapped channels into an editable skeleton with key tracks.
- The FBX path.
- An export CLI that writes the edited image to disk.
