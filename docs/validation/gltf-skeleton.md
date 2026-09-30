# glTF skeleton assembly (M06, fourth slice)

`GltfSkeleton::build(doc, bytes, animation)` (`src/assets/gltf_skeleton.elisa`)
turns a parsed glTF document into an editable skeleton:

- `parent_of(node)`: the node's parent from `nodes[].children`, or -1;
- `depth_of(node)`: the number of links up to the node's root;
- `track_of(node, path)`: the index of the channel of `animation` that
  animates that node's translation, rotation, scale or weights, or -1.
  `GltfKeyTrack` then loads and edits that channel's keys.

It fails with a typed `SkeletonError` in these cases:

- `MissingNodes`: no nodes, or an empty `nodes` array;
- `TooManyNodes`: more than 65,536 nodes;
- `BadChild`: a child out of range, a node that is its own child, or a child
  with two parents;
- `Cycle`: a parent walk does not reach a root within `node_count` steps;
- `MissingAnimation`: the animation does not exist;
- `BadChannel`: a channel that does not map, or whose target node or path
  is out of range;
- `DuplicateChannel`: two channels animate the same node and path.

## Proofs

`proof/gltf_skeleton_index.elisa`: 69/69 proven and replayed.

- `node_ok` accepts only nodes in range.
- `link_ok` accepts a link only when both nodes are in range, they are
  distinct, and the child has no parent yet.
- `track_slot` is -1 or non-negative.
- `walk_ok` bounds a parent walk by the node count.

The upper bound `track_slot < node_count * 4` timed out in the prover, so
the table index is also checked against the table length before every
read and write. That the cycle check finds every cycle is tested, not
proved.

## Tests

`test/assets_gltf_skeleton.elisa`:

- On the Blender fixture, the parents are knee → hip → Armature, the
  depths are 2 and 0, and all six tracks agree with the fixture's channel
  table. Unanimated paths and unknown nodes return -1.
- Each error is triggered from a small JSON document: a shared child, a
  self child, a child out of range, a two-node cycle, no nodes, empty nodes,
  and no animation.
- Retargeting the fixture's hip-rotation channel to the knee gives
  `DuplicateChannel`, retargeting it to node 7 gives `BadChannel`, and
  restoring it builds again.

Mutation check: removing the cycle, duplicate, depth or self-link check
makes the test fail (exits 13, 17 and 4, and a postcondition panic).

## Open

- Evaluating the skeleton's local and world poses from the tracks at a
  given time.
- The FBX path.
- An export CLI that writes the edited image to disk.
