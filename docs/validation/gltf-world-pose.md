# glTF world pose (M06, eighth slice)

`GltfWorldPose` (`src/assets/gltf_world_pose.elisa`) poses a skeleton at one
time.

- `locals` builds each node's local TRS table. It starts from the node's rest
  values (`GltfRestPose`), then overwrites every path that has a channel
  (`GltfSkeleton::track_of`) with that channel sampled at the time
  (`GltfTrackSample`).
- `matrix` builds a column-major T·R·S matrix, as glTF does. `multiply`
  gives the column-major product.
- `compose` multiplies each node's matrix by its parents' matrices, walking
  up the chain. The walk is bounded by the node count, so a bad parent table
  cannot loop forever.
- `pose` raises `Mismatch` when the rest table and skeleton have different
  node counts. It raises `Unreadable` when a channel cannot be sampled.
- `world_at(pose, node, row, column)` returns NaN outside the pose.

## Proof

`proof/gltf_world_index.elisa` proves `GltfWorldIndex` (77/77, all replayed):

- matrix entries stay within 0..15 and are rejected for out-of-range rows or
  columns;
- world slots are rejected past the node count or past 16 entries;
- the parent walk stops at a root and after node-count steps.

## Tests

`test/assets_gltf_world_pose.elisa` checks:

- a 90° rotation about Y, identity products, and that T·S differs from S·T;
- the Blender fixture posed at 0, 0.1, 0.2 and 0.3 s, matched to 1e-7 against
  an independent Python sampler and composer;
- a three-level chain with general rotations and non-uniform scale, against
  Python. The Python matrices were first checked by rotating basis vectors
  with the quaternion directly;
- NaN outside the pose, and the node-count mismatch.

Mutations of the rotation terms, the translation column, the product order,
the parent walk, rest fallback, error choice and path count are all caught.
One gap remains: forcing LINEAR interpolation is not caught here. The fixture's
STEP channels are constant scale tracks. STEP sampling itself is tested in
`assets_gltf_track_sample`.
