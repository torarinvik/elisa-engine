# glTF rest pose (M06, seventh slice)

`GltfRestPose` (`src/assets/gltf_rest_pose.elisa`) reads each node's rest
transform: translation, rotation (xyzw) and scale. A node without one of these
gets the glTF default: zero translation, identity rotation or unit scale.

- `problem(doc, bytes)` returns the first error it finds, as a code:
  - 1: no `nodes` array;
  - 2: more than 65536 nodes;
  - 3: a translation, rotation or scale with the wrong number of elements,
    or with an element that is not a JSON number (a quoted `"1"` is rejected);
  - 4: a rotation whose squared length differs from 1 by more than 0.001;
  - 5: a `matrix` that is not the identity. Decomposing a matrix is still open.
- `build` raises the matching `RestError`. It stores 10 values per node in
  one flat array.
- `value_of(rest, node, path, index)` returns NaN when the node or the
  component is out of range.

## Proof

`proof/gltf_rest_index.elisa` proves `GltfRestIndex` (84/84, all replayed):

- each path has 3 or 4 components, and rotation has 4;
- every field lies inside its node, and negative or out-of-range components
  are rejected;
- slots are rejected past the node count or past the 10 fields.

## Tests

`test/assets_gltf_rest_pose.elisa` checks:

- the Blender fixture: the knee's translation is (0, 1, 0), and hip and
  Armature have default transforms;
- a hand-written node with every member set, and a node whose `matrix` is
  the identity;
- NaN for values outside the node table;
- 12 inputs with problems, including one where the first error must win.

The mutation check found no surviving mutants, with one exception. `slot`'s
`>=` changed to `>` cannot be reached through `value_of`, because `field` never
returns 10. The proof covers that case.
