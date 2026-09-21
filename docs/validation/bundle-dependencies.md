# Bundle dependency declarations

A03 requires the runtime to reject missing dependencies before allocation or
upload. ELPK bundles already carried a dependency manifest, and the virtual
file service already checked it. The render-scene loaders didn't read it, and
projects had no way to declare dependencies. This slice adds both. The native
maze now keeps its wall texture in a second bundle that its tile bundle
declares as a dependency.

## Path

1. **Declare.** An asset cook in `elisa.project.json` lists the bundles its
   output needs, as project-relative paths:

   ```json
   {"importer": "images", "output": "assets/maze_textures.elpk",
    "textures": {"wallalbedo": "assets/maze_wall.png"}},
   {"importer": "gltf", "source": "assets/maze_tile.gltf",
    "asset_path": "assets/maze_tile.gltf", "output": "assets/maze_tile.elpk",
    "dependencies": ["assets/maze_textures.elpk"]}
   ```

   `scripts/elisa_build_run.py` checks each declaration:
   - at most 16 dependencies per cook
   - each one is an `.elpk` path, not repeated, and not the cook's own output
   - the cook's output is an `.elpk` bundle
   - each dependency is in the output's directory or below it
   - another cook in the same project writes it
   - no two cooks write the same output, and the declarations have no cycle

   The runner validates every cook before running any of them. It then cooks
   dependencies first, keeping declaration order where it can.
2. **Cook.** The runner passes each dependency to the cooker as
   `--dependency NAME`, where `NAME` is relative to the output bundle's
   directory. `cook_gltf_asset.py` and `cook_fbx_asset.py` accept the flag for
   `.elpk` outputs only. The new `images` importer runs
   `scripts/cook_image_bundle.py`, which writes a bundle of PNG and JPEG
   sections with no mesh. The writer in `scripts/elisa_package.py` sorts the
   names into the bundle's `manifest` section and rejects unsafe or repeated
   ones.
3. **Resolve.** A manifest names each dependency relative to the directory of
   the bundle that declares it. `native/package_manifest.h` joins the two
   before resolving, so a bundle means the same thing under any root. The
   engine's native test loads the maze bundles from the engine root as
   `examples/maze/assets/...`, and the maze loads them from its own project
   root. Both work. Before this slice, names resolved against the mount root.
   Every existing VFS fixture sits at the mount root, where the two rules
   agree.
4. **Verify.** `native/bundle_dependencies.h` adds
   `verify_bundle_dependencies`. It runs on every ELPK bundle before a mesh or
   texture is read from it:
   - `RenderScene::create_mesh`
   - `RenderScene::register_snapshot_mesh_asset`
   - `RenderScene::register_snapshot_bundle_texture_asset`

   It walks the whole dependency closure with `probe::package_dependency_order`
   under the project root. Every dependency must exist as a regular file under
   the root, with a valid, CRC-checked manifest. The closure must have no
   cycle and at most 16 bundles. Loose `.pkg` files have no manifest and pass
   unchanged. A failure is `AssetLoadFailure`, before any section is read or
   decoded. Dependencies are checked, not loaded; registering them is still
   the caller's job.

The closure bound used to count only finished dependencies. A depth-first walk
finishes nothing until it reaches a leaf, so a long chain was never bounded
and recursion depth was unlimited. The bound now also counts bundles still on
the search path. The virtual file service shares this code, so the fix applies
there too.

A03 also requires decompression bombs to be rejected before allocation. The
reader already sized each zstd output buffer from the index entry, which is
bounded at 64 MiB, and required the decoded size to match. No test covered
this; the package probe now does.

## Evidence

`test/render_scene_bundle_dependency_native.elisa` runs inside the SDL3/Metal
native smoke. Exit codes are `200 + case`, and no other group in the smoke
exits 201–226; see [Exit codes](#exit-codes).
`scripts/bundle_dependency_fixtures.py` first writes its bundles to
`build/cooked/dependencies`. Each bundle holds a 2×1 PNG `albedo` section, so
registering it as a texture succeeds exactly when its dependency closure is
valid.

| Case | Expected | Logged reason |
| --- | --- | --- |
| `leaf.elpk`, no dependencies | OK | |
| `root.elpk` → `leaf.elpk` and `textures/detail.elpk` → `shade.elpk` (`textures/shade.elpk`) | OK; resolved against a root-level name, `leaf.elpk` and `shade.elpk` would be missing | |
| `chain/link-1.elpk`, a chain of 16 dependencies | OK | |
| `missing.elpk` → `absent.elpk` | `AssetLoadFailure` | package dependency is missing |
| `cycle-a.elpk` ↔ `cycle-b.elpk`, and `self.elpk` → itself | `AssetLoadFailure` | package dependency cycle |
| `escape.elpk` → a symlink to a valid bundle outside the root | `AssetLoadFailure` | package dependency is missing; the walker reports the resolver's "outside mount root" rejection this way |
| `corrupt-manifest.elpk` → a bundle whose manifest fails its CRC-32 check; both names the manifest could hold exist | `AssetLoadFailure` | binary section checksum mismatch |
| `parent.elpk`: a checksummed manifest naming `../render-scene-textures.elpk`, which exists | `AssetLoadFailure` | package manifest dependency order rejected |
| `chain/link-0.elpk`, a chain of 17 | `AssetLoadFailure` | package dependency count exceeded |
| retained texture bytes after the rejections | unchanged | |
| snapshot mesh from `mesh.elpk`, cooked with `--dependency leaf.elpk` | OK | |
| snapshot mesh from `mesh-missing.elpk`, cooked with `--dependency absent.elpk` | `AssetLoadFailure` | package dependency is missing |
| `create_mesh` from `mesh-missing.elpk` | `AssetLoadFailure`; instance count unchanged | package dependency is missing |
| `create_mesh` from `mesh.elpk`, then destroy | OK; instance count restored | |
| unregister everything | retained bytes return to the baseline | |

Other checks:

- `scripts/packaged_maze_smoke.py` now stages both maze bundles outside the
  checkout. Three cases are new:
  - the texture bundle is missing (exit 17)
  - one byte of `wallalbedo` in the texture bundle is flipped (exit 17)
  - the tile bundle is re-cooked to depend on `maze_missing.elpk` while the
    texture bundle stays intact, so only the dependency check can fail it
    (exit 17)
- `test/maze_rendering_native.elisa` and the maze client register the tile and
  texture bundles separately. The wall's base color is still 32×32.
- `native/package_bounds_probe.h`, in the native gate:
  - a 16-bundle chain orders all 16 dependencies
  - a 17-bundle chain fails with `package dependency count exceeded`
  - a nested bundle's dependency names resolve against its own directory
  - a zstd bomb: one frame that decodes to 80 MiB of zeros. With an index entry
    admitting 80 MiB, the index fails its 64 MiB section bound. Declared as
    4 KiB, the index is valid, but decoding into the 4 KiB buffer fails with
    `zstd section decompression failed` and keeps no bytes.
- `scripts/test_elisa_build_run.py` checks that an image bundle cooks before
  the glTF bundle that depends on it, and that the glTF cook receives
  `--dependency textures/wall.elpk`. It rejects ten bad declarations.
- `scripts/cook_gltf_asset.py --self-test` requires sorted dependency lines. It
  rejects a dependency with a `.pkg` output, and an escaping, absolute or
  repeated dependency.
- `scripts/cook_image_bundle.py --self-test` requires identical bytes from two
  cooks, and no `mesh` section. It rejects an empty bundle, a `mesh` section, a
  `.pkg` output, an escaping dependency and a missing image.

## Mutation checks

Each mutation was applied to a copy of `native/`. The render smoke's C++ host
was rebuilt from that copy, linked against the Elisa archive the full smoke had
just compiled, and run against the same fixtures. An unmutated control built
the same way exited 0. The checkout's sources weren't edited. These runs came
before the asynchronous asset test existed, so each exit below is this group's
case plus 200.

| Mutation | Result |
| --- | --- |
| the closure bound counts only finished bundles again (`visited.size() >= max`) | exit 226 (case 26): the 17-bundle chain registers |
| `register_snapshot_bundle_texture_asset` skips the dependency check | exit 220 (case 20): `missing.elpk` registers |
| `create_mesh` skips the dependency check | exit 209 (case 9): `mesh-missing.elpk` creates an instance |
| `register_snapshot_mesh_asset` skips the dependency check | exit 208 (case 8): `mesh-missing.elpk` registers |
| dependency names resolve against the mount root again | exit 202 (case 2): `root.elpk` can't find `leaf.elpk` |

## Limits

- **Presence only.** The check proves the closure exists and is well formed.
  It doesn't load or register dependencies, and it doesn't prove that the
  caller registers the textures a mesh needs.
- **Per load.** The closure is walked again on every registration. It isn't
  cached. It is at most 17 small manifest reads.
- **Directory layout.** A bundle can depend only on bundles in its own
  directory or below it, because manifest names can't contain `..`.
- **Time of check.** A dependency can change between the check and a later
  load. The loaders re-read and CRC-check the sections they use.
- **Error detail.** The walker reports every dependency it can't resolve as
  missing, including one the resolver rejects for escaping the root. The
  loaders log that reason and return `AssetLoadFailure` either way.
- **Declared, not inferred.** Cookers don't infer dependencies from source
  files; the project declares them.

## Validation on 2026-09-21

- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal: every case in
  the table above, the maze snapshot test, the maze application smoke and all
  nine packaged maze cases.
- `elisascript scripts/wicked_probe.elisascript frame` passed with the three
  new package-probe checks. The `build` phase compiled the probe and passed
  `scripts/test_elisa_build_run.py`; its application smoke then hit
  elisascript's process time limit under a load average above 150, so the
  application smoke was not part of this run.
- The five mutations above failed with the listed exits.
- `scripts/cook_gltf_asset.py --self-test`, `scripts/cook_fbx_asset.py
  --self-test`, `scripts/cook_image_bundle.py --self-test` and
  `scripts/test_elisa_build_run.py` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17/17 and 6/6). A first run hit the process time limit under the same load;
  the retry passed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work, so its
  stage1 binary was older than its sources. `ELISA_ALLOW_STALE_STAGE1=1` used
  the existing binary.
- That binary exits 2 with no diagnostic when a typed-error `catch` arm
  returns beside a `value: value` arm. The native test avoids that form.

## Exit codes

The asynchronous asset test first exited `216 + case`, which overlapped this
group's exits 220–226. It now exits 194 and logs its case. Every `return` in
the render smoke's test files was checked on 2026-09-21: only this group
produces exits 201–226.
