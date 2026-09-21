# Snapshot-authored assets

`RenderScene::SnapshotPresenter` resolves stable mesh and material IDs through
the typed registration API in `src/runtime/render_scene_assets.elisa`. Mesh
registration loads a project-confined cooked geometry package once and shares
it among snapshot instances. Material registration currently publishes PBR
scalar factors, alpha mode/cutoff, and double-sided state. A resource cannot be
unregistered while a live snapshot instance references it. Failed instance
creation rolls back any entities created for that instance before the snapshot
transaction is retried.

The maze project is the first client of this path. Its project manifest cooks
`assets/maze_tile.gltf` to `assets/maze_tile.pkg`; Elisa registers that mesh and
its authored PBR materials before publishing the first snapshot. The native
SDL3/Metal smoke verifies cooked vertex/index counts, material factors, live
unregister rejection, rollback and retry, stable identity updates, despawn,
and resource release after clear.

The current glTF geometry cooker deliberately supports one untransformed mesh
node, one indexed triangle primitive, and POSITION/NORMAL/optional
TEXCOORD_0. It rejects transforms, skins, morph targets, source material
bindings, unsupported vertex attributes, and extensions. Snapshot material
descriptors with texture IDs are rejected until texture registration and GPU
upload are connected. These are explicit partial limitations, not completed
glTF scene or production material support.

Run the focused gates from the repository root:

```sh
python3 scripts/cook_gltf_asset.py --self-test
python3 scripts/render_scene_native_smoke.py
```

Validation on 2026-09-21: both commands passed on macOS SDL3/Metal. The native
gate rendered the registered triangle, completed the snapshot rollback/retry
and cleanup checks, then built and ran the authored maze project. The full
`elisascript scripts/check.elisascript` suite passed, including both Elisa
Proof suites (17/17 and 6/6); `python3 scripts/test_elisa_build_run.py`,
`python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`,
and `git diff --check` also passed.
