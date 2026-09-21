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

## Retained rows

A staged row with a nonzero `existing_handle` retains that instance. Commit
only moves a retained instance and sets its tint; it never rebinds the mesh or
material. The row must therefore name the instance's own:

- gameplay epoch and gameplay ID
- render ID
- mesh ID
- material or material-set ID; a set is a different ID from the paint it lists

Otherwise `elisa_render_scene_v1_snapshot_stage` returns `INVALID_ARGUMENT`.
Before this check, staging validated only the handle and the transform, so commit
kept the old geometry and material but overwrote the instance's recorded IDs
with the row's, and the scene then misreported what it drew.

This check runs before asset resolution. A changed mesh that isn't resident is
therefore `INVALID_ARGUMENT`, not `ASSET_PENDING` or `ASSET_LOAD_FAILED`. Like
other staging refusals, it records nothing and leaves the transaction open for
the caller to abort. To change any of these IDs, retire the handle and stage a
new row in the same transaction.

`RenderSnapshotScene::sync` follows this contract. It retains a binding only when
the gameplay epoch and ID, render ID, mesh and material all match. It replaces
the instance for any other change, including a render ID that passes to another
gameplay entity or to a later world epoch. Before this change the presenter
compared only the render ID, mesh and material, so either identity change would
have staged a refused row.

`test/render_scene_snapshot_retained_native.elisa` runs in the render smoke's
snapshot group. A failure logs `render scene test group 195 failed at case N`
and exits 195. It registers two meshes, two paints and two sets, each listing
one paint. It then commits one row naming a paint and one naming a set, both for
the same gameplay entity.

| Cases | Check |
| --- | --- |
| 1–8 | Setup: register assets, create both rows, check handles, positions and identity, and finally retire both rows. |
| 11–17 | Restaging both rows with the same IDs keeps both handles, moves both instances, keeps their identity, and makes 4 API calls (begin, 2 rows, commit). |
| 111–194 | Each change is refused with `INVALID_ARGUMENT`, in this order: another mesh (110s); a mesh that isn't registered (120s); another paint (130s); paint to set (140s); another set (150s); set to paint (160s); the next epoch (170s); another gameplay ID (180s); another render ID (190s). After each refusal the instance's own row still stages in the same transaction, which proves the refused row was not recorded. After the abort the instance keeps its position and identity. |
| 201–220 | The presenter, driven through `RenderSnapshot` and `RenderSnapshotScene::sync`: creating and moving make 3 API calls each; a paint change, paint to set, set change, mesh change, and a render ID passing to another entity and then to a later epoch each replace the instance (4 calls) with the new identity; a final move is retained. |
| 301–309 | All assets unregister, and the instance and object counts return to their starting values. |

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

Retained-row validation on 2026-09-21 used `ELISA_ALLOW_STALE_STAGE1=1`, because
the sibling compiler's sources are newer than its stage1 binary. The checks ran
in a detached worktree of HEAD plus this change, which kept out edits another
session was making in the main tree at the same time. Results:

- `python3 scripts/render_scene_native_smoke.py` exited 0 on macOS SDL3/Metal,
  including group 195.
- Deleting the native `snapshot_row_retains_instance` check failed the smoke with
  exit 195 at case 112, because another mesh was staged.
- Making the presenter ignore gameplay identity failed it with exit 195 at case
  213. There the presenter retained a render ID that had passed to another
  entity, and native staging refused the row.
- The full `elisascript scripts/check.elisascript` suite passed, including both
  Elisa Proof suites (17/17 and 6/6).
- `python3 scripts/check_source_length.py` and `git diff --check` passed.
