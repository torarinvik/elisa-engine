# glTF material subsets

A05 asks for multiple primitive and subset material binding. Until this
slice, the glTF geometry cooker accepted one triangle primitive and rejected
any source material binding. Every snapshot mesh drew with one material. Now
a cooked mesh carries up to 16 index subsets, each bound to one of up to 16
material slots. A snapshot row can name a material set that supplies one
registered material per slot, and each subset draws with its slot's material.

## Path

1. **Cook.** `scripts/cook_gltf_geometry.py` accepts one untransformed static
   mesh node with 1–16 triangle primitives.
   - Each primitive becomes one subset: a contiguous run of the index stream,
     in primitive order.
   - Primitives that name the same vertex accessors share one copy of those
     vertices. Primitives that share positions but differ in any other
     attribute fail. Index types can be uint8, uint16 or uint32. Missing
     normals are generated per vertex block.
   - The document's materials become the slots, one per material, at most 16.
     Either every primitive names a declared material, or the document
     declares none and the mesh has one slot.
   - A material with anything beyond a `name` fails, because the cooker binds
     slots and does not import material properties yet. Non-triangle modes,
     attribute tables that aren't accessor indices, and more than 16
     primitives or materials also fail.
2. **Package.** A v2 package records `material_slots`, `subset_count`,
   `subset_stride=12` and `subsets_b64`, which holds one little-endian
   `(index start, index count, slot)` triple per subset. A package without
   these keys is a legacy package with one subset covering every index.
3. **Load.** `detail::parse_geometry_subsets` in
   `native/cooked_geometry_package.h` requires all four keys or none.
   - Slots must be 1–16, subsets 1–16 and the stride 12.
   - Subsets must partition the index stream in order: each starts where the
     previous one ended, holds a nonzero multiple of 3 indices, stays inside
     the stream, and the last one ends at its end.
   - Each subset's slot must be below `material_slots`.
   - Skinned geometry uses the same subset partition. Its armature and joint
     hierarchy remain owned by each instance, while every subset resolves to
     the registered material for its slot.

   The field decoders moved to `native/cooked_package_fields.h` to keep the
   loader under the source-length limit.
4. **Register a set.**
   - `RenderScene::SnapshotMaterialSet` is built with
     `snapshot_material_set_append`. It holds 1–16 material IDs in slot
     order; a zero ID is `InvalidValue` and a seventeenth is `Capacity`.
   - `RenderScene::register_snapshot_material_set(id, set)` registers it.
     Because the C ABI takes no arrays, the Elisa wrapper stages each slot
     with `elisa_render_scene_v1_stage_snapshot_material_set_slot` and then
     registers the count. Slots must be staged in order from 0. Any rejected
     call clears the staging, and registering consumes it.
   - Every listed material must already be registered (`AssetLoadFailure`).
   - Sets share the material ID space, so a set ID can't be a material's and
     a material ID can't be a set's (`InvalidValue`).
   - Registering the same ID again with the same materials is a no-op; with
     different ones it is `InvalidValue`. The table holds 32 sets.
5. **Stage.** A snapshot row names a mesh and either a material or a set.
   - A material fills every slot.
   - A set must list exactly as many materials as the mesh has slots, or the
     row is `InvalidValue`. An unknown ID is `AssetLoadFailure`.
   - A failed row aborts the transaction as before.
6. **Draw.** A static mesh is shared per (mesh, material-or-set) pair. Its
   Wicked mesh gets one `MeshSubset` per cooked subset, bound to the entity
   of that slot's registered material. The object casts a shadow unless
   every subset's material is alpha-blended.
7. **Unregister.**
   - A material listed by any set can't be unregistered (`InvalidValue`).
   - A set can't be unregistered while an instance or shared mesh draws
     with it.
   - Sets can't change during a snapshot transaction (`BatchActive`).

## Evidence

`test/fixtures/multi_material_panel.gltf` is a 2×2 panel of three strips in the y=0
plane. The two edge strips share one vertex block with normals and UVs, and
index it with uint16 and uint32 indices. The center strip has its own block
with positions only, so its normals are generated, and uint8 indices. The
primitives bind materials `edge`, `center`, `edge`, which are slots 1, 0, 1.
It cooks to 12 vertices and 18 indices with subsets (0, 6, 1), (6, 6, 0) and
(12, 6, 1).

**Cooker.** `cook_gltf_asset.py --self-test` cooks the panel twice. It checks
that the result is byte-identical, then checks the counts and the subset
records. Nine variants must fail for the stated reason:
- a primitive naming a missing material
- a material index outside the document
- a material with properties
- 17 materials
- 17 primitives
- line mode
- two primitives sharing positions with different attributes
- an attribute table given as a list
- an attribute given as a list

**Loader.** `scripts/test_geometry_subsets.py` builds
`native/geometry_subset_test.cpp` against the production loader under
AddressSanitizer and UndefinedBehaviorSanitizer. It runs 83 cases, each of
which must be accepted with exactly the expected subsets or rejected with the
expected error:
- accepted: the cooked panel as `.pkg` and `.elpk`, the maze tile, a legacy
  package with no records, an explicit single subset, a mesh with an unused
  slot, 16 subsets in 16 slots, the generated multi-material skinned and
  morphed glTF panels, and skinned meshes with no records, one subset, or
  multiple subsets and slot materials, plus camera/light metadata records
- rejected: a gap, an overlap, a subset that splits a triangle, an empty
  subset, a missing slot, a short total, an overrun, a count that wraps
  `uint32`, four subsets whose wraps land back on an exact partition, 17
  subsets, 17 slots, 0 slots, a count that disagrees with the
  records, stride 16, a missing `material_slots`, and missing records

The render smoke runs it, and so does the Wicked probe's build phase.

**Native.** `test/render_scene_material_subset_native.elisa` runs in the
SDL3/Metal smoke before the asynchronous asset test. The render smoke has run
out of distinct exit codes, so this group exits 193 and logs its failing case
through `elisa_render_scene_v1_test_failed_case`. The smoke cooks the panel to
`build/cooked/subsets/panel.elpk` and writes a two-triangle skinned strip with
two material subsets to `build/cooked/subsets/skinned.pkg`. The test registers emissive, double-sided
red and blue paints, plus alpha-blended glass versions of each. Test hooks
built only into the smoke host read back:
- each instance's Wicked subsets and their material entities
- its shadow flag
- the live set count
- the dominant color channel of a 5×5 patch of the last 3D frame

The frame is read from the R11G11B10 render target.

The group logs its first failing case. The cases are:

| Cases | What they check |
| --- | --- |
| 1–8 | No sets at start; the panel, the maze tile and the skinned strip register; four paints register |
| 11–24 | Six sets register and count 6. Registering the same materials again is a no-op; different materials or a different count conflict. A set listing a missing material is `AssetLoadFailure`. A set can't reuse a material's ID, nor a material a set's |
| 31–45 | The raw ABI refuses an out-of-order slot, a slot after a rejection, a count that disagrees with the staging, a zero material, a zero count and slot 16. A full 16-slot set registers, registering consumes the staging, and a second unregister is `AssetLoadFailure` |
| 51–57 | The Elisa builder refuses an empty set, a zero ID and a seventeenth material; a 16-material set registers |
| 61–68 | Staging a panel row with a one- or three-material set, or a tile row with a two-material set, is `InvalidValue`. The two-material set stages on the two-subset skinned row. An unknown set and a set whose material was never registered are `AssetLoadFailure`. A one-material set on the tile and a plain material on the panel stage |
| 71–82 | A panel row with `[blue, red]` draws subsets (0, 6, red), (6, 6, blue), (12, 6, red) and casts a shadow. The frame shows a blue center strip between red ones. The set in use and a material a set lists can't be unregistered. Replacing the row with `[red, blue]` swaps the frame's colors, and the replaced set can then be unregistered and registered again |
| 91–101 | One commit creates a glass panel, a mixed panel, a single-material panel and a skinned strip. Shadows are off only for the all-glass panel; every panel and both skinned subsets draw with their expected materials. The mixed set can be unregistered only after its row is retired |
| 111–115 | During a transaction, registering, unregistering, staging a slot and registering staged slots are all `BatchActive`, and they leave no staging behind |
| 121–122 | The table accepts sets up to 32 and refuses the next with `Capacity` |
| 131–140 | Every set, paint and mesh unregisters, and the shared-mesh and instance counts return to where they started |

## Mutation checks

The render mutations were applied to a copy of `native/`. The smoke's C++
host was rebuilt from that copy, linked against the Elisa archive from the
full smoke, and run against the same fixtures. An unmutated control built the
same way exited 0. Every mutant exited 193 at the listed case:

| Mutation | Failing case |
| --- | --- |
| every subset draws with slot 0's material | 72: the panel's subsets don't match their slots |
| the Wicked mesh gets one subset covering every index | 72 |
| staging skips the set-size check | 61: a one-material set stages on the panel |
| a material listed by a set can be unregistered | 76 |
| a set in use can be unregistered | 75 |
| the shadow flag looks only at slot 0's material | 94: the mixed panel casts no shadow |
| registering a set ID again with different materials succeeds | 19 |
| a rejected register leaves the staging behind | 36: after a count of 3 is refused, a count of 2 registers |
| shared static meshes ignore the set | 78: the second row reuses the first row's mesh and colors |
| skinned rows use the old `MAX_INSTANCES` sentinel again | 93: the commit fails |
| a set may take a registered material's ID | 22 |

The loader mutations were applied to a copy of
`native/cooked_geometry_package.h` and run through the sanitized loader test.
The control passed all 26 cases. Each mutant failed the listed cases:

| Mutation | Failing cases |
| --- | --- |
| no check that a subset starts where the previous one ended | gap, overlap |
| empty subsets allowed | empty subset |
| counts need not be a multiple of 3 | split triangle |
| no per-subset bound against the index stream | triple wrap |
| no check that the last subset ends at the stream's end | short total |
| no slot bound | missing slot |
| partial records read as a legacy package | missing `material_slots`, missing records |
| 17 subsets allowed | 17 subsets |
| any stride allowed | stride 16 |

The per-subset bound first survived: a single wrapping count also broke the
running total, so the total check caught it anyway. The triple-wrap case has
four subsets whose sums wrap `uint32` back to an exact partition, so only the
per-subset bound rejects it.

## Findings

- **Skinned snapshot rows couldn't commit.** Shared static meshes marked a
  row without a shared mesh with `MAX_INSTANCES`, but the retain loop skips
  only slots at or above `MAX_SNAPSHOT_SHARED_MESHES`. Every skinned row
  looked like a shared-mesh row, and the commit failed with `BackendFailed`.
  No test staged a skinned snapshot row until case 93. One `NO_SHARED_MESH`
  sentinel now marks such rows everywhere.
- **Tonemapping shifts hue.** The smoke tonemaps with ACES. Bright saturated
  paints came out with the wrong dominant channel: pure blue picked up red,
  and ambient light tinted a bright base color. The paints now use a dim base
  color and unit emission. The probe row also moved to 30% down the frame,
  because the main test's electric arc crosses the middle row.
- **Pipelines compile late under load.** Wicked compiles object pipelines on
  worker threads and skips a draw until its pipeline is ready. On a loaded
  machine the frame probe ran at render frame 12 with 28 pipeline jobs still
  pending, so every mesh was missing and case 73 failed. The existing pixel
  probe waited at most 0.4 s. Both probes now share a wait of up to 30 s, and
  eight runs beside 12 busy processes passed.

## Limits

- **No material properties from glTF.** Slots carry only their order. The
  game registers each slot's material in Elisa. Superseded for factors by
  [`cooked-slot-materials.md`](cooked-slot-materials.md); textures remain.
- **Bounded skin importer.** The runtime glTF cooker now emits v3 packages for
  one skin with up to 64 parent-ordered TRS joints, four float32 influences per
  vertex, and up to 16 material subsets within the existing bounds.
  Inverse-bind accessors are validated; the native uploader derives the same
  bind relation from the stored rest transforms. Sampled LINEAR and STEP TRS
  channels become fixed 30 Hz clips (up to sixteen clips and 3,601 frames per
  clip). Dense POSITION deltas, with optional NORMAL deltas, carry up to 32
  morph targets into Wicked and accept bounded weight submissions. Morph
  animation channels and cubic-spline channels remain rejected. Mesh-node
  transforms must be identity for skinned packages. The package loader
  validates bounded camera and `KHR_lights_punctual` records, and direct
  `RenderScene::create_mesh` binds those authored resources through opaque
  imported-scene handles; independent placement entities remain limited to
  static, non-morphed geometry.
- **One mesh node.** Scene hierarchies, node transforms, multiple meshes,
  camera/light metadata, skins, morphs, and animation are carried by the
  bounded runtime cooker. Superseded for the hierarchy and live imported-scene
  path by [`gltf-node-hierarchies.md`](gltf-node-hierarchies.md); skinned
  placement entities remain constrained to the flattened compatibility path.
- **Shared per set.** Two sets that list the same materials still create two
  shared Wicked meshes.
- **Shadow policy.** An object casts a shadow when any subset is not
  alpha-blended; Wicked has no per-subset shadow flag.

## Validation on 2026-09-21

- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal. That covers
  every case above, the loader's 26 cases, the earlier snapshot,
  bundle-texture, bundle-dependency and asynchronous asset tests, the maze
  snapshot test, the maze application smoke and all nine packaged maze cases.
- Before the pipeline wait, case 73 failed on the second run beside 12 busy
  processes. With the wait, eight runs under that load passed.
- `elisascript scripts/wicked_probe.elisascript build` ran the cooker self-test
  and the loader test, then passed its application and render smokes. The
  `frame` phase then passed.
- `scripts/run_boundary_sanitized.py` passed with no AddressSanitizer, UBSan
  or ThreadSanitizer finding.
- The eleven render mutations and ten loader mutations above failed as
  listed. Both controls passed.
- A syntax-only compile of `native/render_scene_abi.cpp` without
  `ELISA_RENDER_SCENE_TEST_PROBE` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17 and 6 obligations, none failed).
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
