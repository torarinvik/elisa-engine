# Snapshot-authored assets

`RenderScene::SnapshotPresenter` resolves stable mesh and material IDs through
the typed registration API in `src/runtime/render_scene_assets.elisa`. Mesh
registration loads a project-confined cooked geometry package once. Static
snapshot objects share one Wicked `MeshComponent` for each mesh/material ID
pair; each object keeps its own transform. The material is bound by the shared
mesh subset, so the material ID is part of the sharing key. Separate instances
using the same pair retain that mesh until the last instance is removed. Skinned
meshes still create per-instance mesh, armature, and joint resources.

Material registration publishes PBR scalar factors, alpha mode/cutoff,
double-sided state, and registered texture IDs through a Wicked material
entity. Snapshot texture registration resolves paths inside the project root;
base-color, normal, packed surface, and emissive maps are applied through
Wicked's resource manager. The adapter has one packed surface slot, so distinct
metallic-roughness and occlusion maps must be cooked together. Mesh, material,
and texture assets cannot be unregistered while a live snapshot instance or
material references them. Failed snapshot creation rolls back newly created
objects, joints, and unreferenced shared meshes while preserving the previous
frame.

The maze project is the first client of this path. Its project manifest cooks
`assets/maze_tile.gltf` to `assets/maze_tile.elpk`; Elisa registers the bundle's `mesh` section and
its authored PBR materials before publishing the first snapshot. The native
SDL3/Metal smoke verifies cooked vertex/index counts, material factors, two
static instances sharing one Wicked mesh, pair-level mesh counts across
replacement and despawn, live unregister rejection, rollback and retry, stable
identity updates, and release of all shared pairs after clear. It also submits
200 instances of one pair in one snapshot and checks the test-only ABI counter
reports 202 calls: begin, 200 staged rows, and commit. Updating one instance's
transform leaves the other instance at its own position. Despawning one of the
200 leaves 199 instances, the neighbors' positions and material factors, and
one shared pair; that transaction makes 202 calls (begin, 199 rows, one retire,
commit). Clearing the batch releases the shared mesh while preserving the
primitive baseline. Per-instance color and emissive tints leave the shared pair
intact; see [`instance-tints.md`](instance-tints.md).

The current glTF geometry cooker deliberately supports one untransformed mesh
node, one indexed triangle primitive, and POSITION/NORMAL/optional
TEXCOORD_0. It rejects transforms, skins, morph targets, source material
bindings, unsupported vertex attributes, and extensions. Snapshot texture IDs
must be registered separately from scalar material descriptors; only one
packed Wicked surface map is supported, and nonidentical metallic-roughness and
occlusion IDs are rejected until a cooker merges them. Static mesh/material
pairs share their Wicked geometry. Skinned snapshot resources stay per instance
by design: Wicked keeps the armature on `MeshComponent::armatureID`, so objects
sharing a skinned mesh would share one pose. The cooked geometry is still
registered and loaded once. These are explicit partial limitations, not completed glTF scene or
production material support.

Run the focused gates from the repository root:

```sh
python3 scripts/cook_gltf_asset.py --self-test
python3 scripts/render_scene_native_smoke.py
```

Validation on 2026-09-21: both commands passed on macOS SDL3/Metal. The native
gate rendered the registered triangle, verified snapshot texture loading and
live texture-unregister rejection, shared static-mesh references and counts
across rollback/retry, replacement, and cleanup, then built and ran the authored maze project. The full
`elisascript scripts/check.elisascript` suite passed, including both Elisa
Proof suites (17/17 and 6/6); `python3 scripts/test_elisa_build_run.py`,
`python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`,
and `git diff --check` also passed.
