# glTF node hierarchies

Until now the runtime glTF cooker took one untransformed mesh node. A
translated, rotated or scaled node failed, and so did a second mesh node. Now
the cooker bakes a static scene's whole node hierarchy into one cooked mesh.
That covers matrix and TRS nodes, nested groups, several meshes, one mesh
placed more than once, and mirroring scales. The flattened render payload is
still an ordinary cooked mesh with material subsets, while the package now
also carries bounded source mesh/node placement metadata for scene clients.
`RenderScene::create_mesh` uses those ranges to create one Wicked mesh entity
per static placement under the root; the root keeps the public instance handle
and game transform.

## Path

1. **Validate the nodes.** `mesh_placements` in the new
   `scripts/cook_gltf_nodes.py` walks the scene.
   - A document has 1–256 nodes. A node may have only `name`, `mesh`, `skin`,
     `children`, `matrix`, `translation`, `rotation` and `scale`. Camera and
     morph properties, `extras` and extensions fail as unsupported node
     properties. A `skin` field is accepted only by the bounded skin importer
     described in [`gltf-material-subsets.md`](gltf-material-subsets.md).
   - A `matrix` has 16 finite numbers in glTF's column-major order, and its
     bottom row must be 0 0 0 1. It can't appear beside a translation,
     rotation or scale.
   - A translation and scale have 3 finite numbers and a rotation has 4.
     Booleans and strings don't count as numbers. The rotation must be unit
     length within 1e-3, because exporters round it, and it is then
     renormalized. The node's local transform is translation after rotation
     after scale.
   - Children must be node indices in range. A node has at most one parent
     and can't be its own child.
   - The document has exactly one scene, `scene` is 0, and the scene has only
     `nodes` and `name`. Its nodes are distinct, in range and have no parent.
     Every node must be reachable from them, which also rules out cycles, and
     every mesh must be placed by at least one node.
   - Each node that places a mesh must have a finite world transform with a
     finite, nonzero determinant.
2. **Bake.** `normalized_geometry` in `scripts/cook_gltf_geometry.py` bakes
   each placement.
   - Placements come depth first in scene order, a node before its
     children. Each placement's primitives keep their order.
   - Positions go through the world transform. Normals go through its
     cofactor matrix, which is the inverse transpose times the determinant.
     Each normal is multiplied by the determinant's sign and renormalized. A
     primitive without normals gets normals generated from its world
     positions.
   - A mirroring transform, one with a negative determinant, reverses
     triangle winding. The cooker swaps each triangle's last two indices so
     authored front faces stay front-facing.
   - An identity placement copies the source bytes unchanged. A single-node
     asset therefore cooks to the same bytes as before.
   - Primitives of one placement that name the same accessors still share one
     copy of their vertices. Each placement gets its own copy.
   - Adjacent subsets on the same material slot merge, and at most 16 subsets
     may remain. The vertex and index totals are bounded across all
     placements as they grow, before any vertex is transformed.
   - A transformed coordinate outside the `float32` range fails.
3. **Count.** `placed_counts` recounts triangles and positions per placement
   from the accessors, and the cook checks the baked streams against those
   counts. The package's `triangles=` line reports the placed triangles.
4. **Slots.** Material slots are unchanged: one per declared glTF material,
   and each primitive's subset uses its material's slot. The primitives of
   every mesh draw from one slot list.
5. **Retain source placement identities.** `mesh_count` and the
   `mesh_placements_b64` records preserve each source mesh index, node index,
   and world transform in depth-first order. Each fixed 80-byte record also
   stores contiguous vertex and index ranges plus the overlapping subset range.
   The native package reader checks those ranges, finite transforms, and that
   every source mesh is represented before exposing the records to runtime
   clients.

   The public runtime query is `RenderScene::snapshot_mesh_placement_count`
   followed by `RenderScene::snapshot_mesh_placement`. It returns the source
   mesh index, node index, and row-major affine 3x4 world transform without
   re-reading the bundle. Snapshot rows retain the flattened compatibility
   upload, while direct imported meshes now keep independently addressable
   static placement entities.

The public scene boundary exposes authored child resources without leaking
Wicked entities. `RenderScene::imported_scene(handle)` returns bounded mesh,
camera and light counts. `RenderScene::imported_camera(handle, index)` creates
an opaque `ImportedCameraHandle`, and `activate_imported_camera` selects it for
the active render path. The handle retains its root instance generation, so
destroying the root makes later activation return `UnknownHandle`.
`RenderScene::imported_mesh(handle, index)` similarly returns an opaque
`ImportedMeshHandle` for the root or any authored child placement. Its
visibility and local transform can be changed without exposing a Wicked entity;
invalid indices and stale roots remain checked at the native boundary. The
native imported-scene cases use the hierarchy and scene-metadata fixtures in
the SDL3/Metal smoke.

## Evidence

`test/fixtures/node_hierarchy_panel.gltf` has three single-sided materials,
red, green and blue. Each has a base color of 0.08 in its channel, emissive 1
in that channel, metallic 0 and roughness 0.9. Its three strip meshes are the
same quad, x in [-0.25, 0.25] and z in [-1, 1], authored three ways:

| Mesh | Attributes | Faces | Material |
| --- | --- | --- | --- |
| `red_strip` | position, normal | up | red |
| `green_strip_facing_down` | position only | down | green |
| `blue_strip` | position, normal, UV | up | blue |

| Node | Transform | Places |
| --- | --- | --- |
| 0 `panel` (root) | matrix: scale x by 0.5, then translate x by 1/6 | children 1, 4, 5 |
| 1 `left` | translate x by -5/3 | children 2, 3 |
| 2 `left_near` | translate z by -0.5, scale z by 0.5 | red strip |
| 3 `left_far` | translate z by 0.5, scale z by 0.5 | red strip |
| 4 `center` | translate x by -1/3, half turn about z | green strip |
| 5 `right` | translate x by 1, scale x by -1 | blue strip |

Baked, the strips sit at x = -2/3, 0 and 2/3, each a quarter wide, and every
face points up. The two red placements join into one strip. The mesh has 16
vertices, 24 indices and three subsets: red (0, 12), green (12, 6) and blue
(18, 6). The render smoke cooks the fixture to
`build/cooked/subsets/hierarchy.elpk`.

**Cooker.** `scripts/gltf_hierarchy_self_test.py` runs as part of
`cook_gltf_asset.py --self-test`.
- The fixture cooks to the same bytes twice, with 8 triangles, 16 positions
  and three subsets.
- The baked strips have the exact extents above. Every normal is exactly
  (0, 1, 0), and every triangle faces up.
- A quarter turn about +Y carries +X to -Z. For two general rotations with a
  non-uniform scale, TRS and matrix nodes both match a translation after
  Rodrigues' rotation after the scale.
- Three variants bake to the same bytes as the fixture: a TRS root, a
  rotation of length 1.0005, and the negated rotation.
- Wrapping the maze tile's node in a group, with an identity TRS or an
  identity matrix, keeps its bytes. An identity placement also keeps a -0.0
  coordinate and a unit normal that renormalizing would round. The flattened
  geometry stays byte-identical; the retained source metadata correctly
  records the added grouping node.
- Sixteen more red placements after the two red strips merge into one
  subset: (0, 108), then green and blue, with 80 vertices. Fifteen
  alternating red and green placements plus blue keep 16 subsets.
- With the vertex bound lowered to 15 or the index bound to 23, the fixture
  fails for that bound.
- 45 variants must fail for the stated reason:
  - node properties: a camera, a skin, morph weights, `extras`, a node that
    isn't an object
  - matrices: one beside a translation, a projective bottom row, a
    row-major translation, 15 numbers
  - TRS: a rotation of length 2, a rotation with 3 numbers, a NaN
    translation, a boolean scale, a string translation
  - transforms: a zero scale, a collapsing parent, a world scale that
    overflows, a world translation that overflows while its scale stays
    finite, a vertex moved past the `float32` range
  - hierarchy: a child out of range, a string child, children that aren't a
    list, a node with two parents, a root that is its own child, an
    unreachable cycle, an orphaned mesh node
  - scenes: a scene naming a child, a root named twice, an empty scene, a
    scene node out of range, two scenes, no default scene, scene `extras`
  - meshes: an unplaced mesh, a mesh index out of range, a boolean mesh
    index, mesh `extras`, a mesh that isn't an object, a second mesh without
    primitives, a morph target on a placed mesh
  - bounds and document: 257 nodes, 257 meshes, 17 alternating subsets, a
    camera list, an animation list

`cook_gltf_asset.py`'s own self-test used to reject a node translation and a
second mesh node. Those are now accepted, so it rejects a camera node and a
second scene instead.

**Loader.** `scripts/test_geometry_subsets.py` now runs 85 sanitized cases.
The two new ones load the cooked fixture with its three subsets and three
authored slot records, and the 16-subset alternating cook. The imported-scene
smoke also creates the four-placement fixture, checks four Wicked mesh entities
under one public handle, and verifies cleanup after destruction.

**Native.** `test/render_scene_node_hierarchy_native.elisa` runs in the
SDL3/Metal smoke after the cooked-material test, as group 228. The row sits at
y = 4 with scale (12, 1, 8), so the strips land at world x = -8, 0 and 8.

| Cases | What they check |
| --- | --- |
| 1–5 | The baked mesh registers and counts three cooked materials. They register as a red, green and blue set, and each Wicked material holds its authored factors |
| 6–9 | A two-material list for the mesh is `InvalidValue`. A plain two-material set registers, but staging a row with it is refused, because the mesh has three slots |
| 11–12 | The row commits and draws three Wicked subsets: red (0, 12) for both red placements, green (12, 6) and blue (18, 6) |
| 13–14 | The frame shows red on the left, green in the center and blue on the right. The gaps between the strips show none of the three colors |
| 15–18 | A half turn about +Y keeps the row's handle and swaps the sides. A generic pose's Wicked world matrix is the reflected Elisa matrix, and the row retires |
| 21–23, 30 | Every set, material and mesh unregisters, and the shared-mesh, instance and set counts return to where they started |

The strips' materials are single-sided. If the bake got a strip's facing
wrong, the strip would be culled and its color would be missing. The green
strip is only front-facing because of its node's rotation, and the blue strip
only because of the winding swap.

**Screen handedness.** The first run expected red on the frame's left and
failed at case 13. A diagnostic run found blue on the left, green in the
center and red on the right, with both gaps clear. `native/coordinate_conventions.h`
says Elisa's world is right-handed with +Y up and that X is negated once at
the Wicked boundary. The render service doesn't do that negation. It passes
positions, transforms and the camera to Wicked unchanged, and swaps each cooked
triangle's winding instead. The result is a mirror image: under the smoke
camera, world +X shows on the frame's left. This slice didn't change that
behavior. Its test expected the mirrored sides. (Since fixed: see
[`render-scene-handedness.md`](render-scene-handedness.md). The table above
shows the corrected sides.)

## Mutation checks

Forty-two cooker mutations ran on a copy of `scripts/`. Each one ran
`cook_gltf_asset.py --self-test`, then `test_geometry_subsets.py` if the
self-test passed. The control passed both, and every mutant failed the
self-test:
- transforms: no winding swap, no normal sign, unnormalized normals, the
  parent composed after the child, the parent ignored, a matrix's linear part
  read row-major, the rotation ignored, the inverse rotation, scale applied
  before rotation, TRS scale ignored, the rotation not renormalized
- checks removed: the affine check, the matrix-beside-TRS check, the
  rotation length, the second-parent check, the own-child check, the root
  check, the distinct-roots check, the reachability check, the every-mesh
  check, the zero-determinant check, the finite-world check, the
  `float32` range check, the node bound, the mesh bound, the node key check,
  the mesh key check, the scene key check, the upper bounds on mesh and child
  indices, the scene count, the default scene, the camera-list check
- baking: no identity shortcut for positions or for normals, no subset
  merge, no subset bound, placements sharing one vertex copy, no running
  vertex or index bound, normals generated from untransformed positions,
  positions counted once per mesh instead of per placement

Six mutants survived the first round, and the self-test gained five cases
for them:
- Without the own-child check, a root that is its own child still failed,
  but for another reason.
- Without the finite-world check, the later `float32` check still caught an
  overflowing translation.
- Neither identity shortcut changed the maze tile's exact coordinates and
  normals.
- The node bound implied the mesh bound.
- The camera-list check predates this slice and had no test.

Only the local-transform check catches three mutants: the matrix read
row-major, the inverse rotation and scale before rotation. The fixture's root
matrix is diagonal, its only rotation is a half turn, which is its own
inverse, and no node both rotates and scales.

Eight of these mutants also cooked the fixture for the render smoke host,
which was left unchanged from the last full smoke. The control exited 0:

| Mutation | Result |
| --- | --- |
| no winding swap | 228/13: the blue strip is culled |
| rotation ignored | 228/13: the green strip is culled |
| parent composed after the child | 228/13 |
| parent ignored | 228/13 |
| normals generated from untransformed positions | 228/13 |
| no subset merge | 228/12: four subsets |
| TRS scale ignored | exit 0; the self-test catches it |
| no normal sign | exit 0; the self-test catches it |

Ignoring TRS scale leaves every strip visible at its sample point, because
the mirrored quad is symmetric and the red strips only overlap. A wrong normal
sign only changes lighting, and the emissive color still dominates.

## Limits

- **Placement ranges are static.** Skinned and morphed packages continue to
  use the single cooked mesh path, and skinned node transforms remain identity.
  Cameras, lights and animation metadata are imported, but independent
  placement entities currently apply only to static, non-morphed geometry.
- **Snapshot rows remain flattened.** Snapshot material registration and row
  commits still use the compatibility mesh. Direct `create_mesh` imports expose
  placement entities through the root instance lifetime; their transforms are
  baked into each vertex range, so no extra per-placement matrix is required.
- **Bounds.** 256 nodes, 256 meshes, 16 primitives per mesh, 16 material
  slots and 16 subsets after merging. The existing vertex and index bounds
  apply to the baked totals.
- **Mirrored frames.** The render service showed Elisa's right-handed world
  mirrored, as described above. That predated this slice. (Since fixed: see
  [`render-scene-handedness.md`](render-scene-handedness.md).)
- **No textures.** Material textures and `MASK` still fail in the cooker, as
  in [`cooked-slot-materials.md`](cooked-slot-materials.md). (Since
  superseded: see [`cooked-material-textures.md`](cooked-material-textures.md).)

## Validation on 2026-09-21

These ran in a detached worktree on 10bd6f2 that held only this change,
because other sessions shared the main working tree.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal. That covers
  groups 193–199, 227 and 228, the loader's 46 cases, the maze application
  smoke and the packaged maze cases.
- `cook_gltf_asset.py --self-test` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17 and 6 obligations, none failed).
- The cooker and render mutations above failed as listed, and both controls
  passed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
