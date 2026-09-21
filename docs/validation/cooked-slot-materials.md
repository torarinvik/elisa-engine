# Cooked slot materials

The previous A05 slice gave cooked meshes material slots
([`gltf-material-subsets.md`](gltf-material-subsets.md)), but the cooker
rejected any glTF material that had more than a `name`. The game had to
restate every slot's factors in Elisa. Now the cooker packs each material's
glTF factors into its slot, and a game registers a mesh's cooked materials,
or a set of them, by ID.

## Path

1. **Cook.** `scripts/cook_gltf_geometry.py` reads each declared material.
   - It cooks `baseColorFactor`, `metallicFactor`, `roughnessFactor`,
     `emissiveFactor`, `alphaCutoff`, `alphaMode` and `doubleSided`. An absent
     factor takes the glTF default: white, metallic 1, roughness 1, no
     emission, cutoff 0.5, opaque and single-sided.
   - Every factor must be a finite number in [0, 1]. Booleans aren't numbers,
     and color and emissive lists need exactly 4 and 3 entries.
   - `alphaMode` must be `OPAQUE` or `BLEND`. `MASK` fails, because a mask
     tests the base-color texture's alpha and the cooker doesn't cook
     textures.
   - A material or `pbrMetallicRoughness` that names any texture fails with a
     message that says to register textured slot materials at runtime. Any
     other key, such as `extensions` or `extras`, fails as an unsupported
     material property.
   - A document without materials still cooks one slot and no records.
2. **Package.** Beside the subset records, a package with slot materials holds
   `slot_material_stride=48` and `slot_materials_b64`: one little-endian
   record per slot of ten `float32` factors (base color, metallic, roughness,
   emissive, alpha cutoff), the alpha mode (0 opaque, 2 blend) and flags
   (bit 0: double-sided). A mesh with one slot, one subset and no materials
   still omits every record, so older packages are unchanged.
3. **Load.** `detail::parse_slot_materials` in
   `native/cooked_geometry_package.h` runs after the subset parser.
   - The two keys must both be present or both absent.
   - They need explicit subset records, which also rules them out on
     skinned geometry.
   - The stride must be 48 and the records must fill exactly
     `material_slots` × 48 bytes.
   - Every factor must lie in [0, 1], so NaN and infinity fail. The mode
     must be 0 or 2 and the flags may only set bit 0.
   - A failure is `invalid cooked slot material records` or
     `cooked slot material is out of range`.
4. **Count.** `RenderScene::snapshot_mesh_material_count(mesh)` returns the
   number of cooked slot materials of a resident mesh: 0 when its source
   declared none, otherwise its slot count. A zero ID is `InvalidValue`. A
   mesh that is still loading is `AssetPending`, and one that isn't
   registered is `AssetLoadFailure`. Counting works inside a snapshot
   transaction.
5. **Register one slot.**
   `RenderScene::register_snapshot_mesh_material(mesh, slot, material)`
   registers a slot's factors as an ordinary material. It follows the
   material rules. The same factors under the same ID are a no-op, while
   different factors, a set's ID or a slot past the count are
   `InvalidValue`. It can't run during a snapshot transaction
   (`BatchActive`).
6. **Register a whole mesh.**
   `RenderScene::register_snapshot_mesh_materials(mesh, materials, set)`
   takes one material ID per cooked slot, in slot order.
   - It registers each slot's factors under its ID. A slot whose ID already
     holds the same factors reuses that material.
   - It then registers those IDs as material set `set`.
   - The list must match the cooked count.
   - If any slot or the set fails, every material this call created is
     removed again. Materials that already existed stay, and the staging is
     consumed either way.

   The Elisa wrapper stages the IDs through
   `elisa_render_scene_v1_stage_snapshot_material_set_slot` and then calls
   `elisa_render_scene_v1_register_snapshot_mesh_material_set`.
7. **Share code.** Material registration moved into
   `register_snapshot_material_locked` in
   `native/render_scene_snapshot_material_internal.inc`, and set registration
   into `register_snapshot_material_set_locked`. The existing material and
   set ABIs and the new mesh-material ABIs use the same checks.

## Evidence

`test/fixtures/multi_material_panel.gltf` now authors both of its materials:

| Material | Base color | Metallic | Roughness | Emissive | Alpha | Sides |
| --- | --- | --- | --- | --- | --- | --- |
| `center` (slot 0) | (0, 0, 0.08, 1) | 0 | 0.9 | (0, 0, 1) | opaque by default | double |
| `edge` (slot 1) | (0.08, 0, 0, 1) | 0.25 | 0.75 | (1, 0, 0) | `OPAQUE` | double |

The render smoke also writes `build/cooked/subsets/glass_panel.gltf`, whose
center is `BLEND` with alpha 0.5 and single-sided, and cooks it to
`glass_panel.pkg`.

**Cooker.** `cook_gltf_asset.py --self-test` cooks the panel twice and checks
that the bytes match and both slot records hold the authored values exactly.
Three variants must cook the expected records:
- a name-only material, which cooks the glTF defaults
- the glass center
- a single primitive with one material

Twenty-nine variants must fail for the stated reason. Eight are the earlier
geometry variants. The other 21 are material variants:
- a base-color, metallic-roughness, normal, occlusion or emissive texture
- `MASK`, or an unknown alpha mode
- an extension, `extras`, or an unknown PBR property
- a material, or its `pbrMetallicRoughness`, that isn't an object
- a base color above 1, or with 3 entries
- a negative roughness
- a NaN or boolean metallic factor
- a string emissive channel, or an emissive factor with 2 entries
- an alpha cutoff above 1
- a string `doubleSided`

**Loader.** `scripts/test_geometry_subsets.py` now runs 44 sanitized cases. An
accepted case can list each slot's expected record, and one without records
must load no slot materials.
- The cooked panel `.pkg` and `.elpk` load both authored records.
- Accepted:
  - two hand-built records
  - a single-slot mesh with explicit subsets and one record
  - records at the bounds: all zeros, and all ones with blend and
    double-sided
- Rejected:
  - records without subset records
  - records with malformed or incomplete subset metadata
  - stride 44
  - a stride without records, and records without a stride
  - one record too few or too many
  - a factor above 1, negative, NaN or infinite
  - an alpha cutoff above 1
  - mode 1 (mask) and mode 3
  - flags 3

**Native.** `test/render_scene_cooked_material_native.elisa` runs in the
SDL3/Metal smoke after the asynchronous asset test, as group 196. The test hook
`elisa_render_scene_v1_test_snapshot_material_matches` compares a registered
material with the expected factors. It checks both the stored registration
and the Wicked material it created:
- base color, metalness, roughness and emissive color
- the alpha reference: the cutoff for a mask, otherwise opaque
- sidedness
- blending
- shadow casting, which blended materials turn off

| Cases | What they check |
| --- | --- |
| 1–4 | The panel, the glass panel, the maze tile and the skinned strip register |
| 11–15 | Both panels count 2 cooked materials; the tile and the skinned strip count 0. An unregistered mesh is `AssetLoadFailure`, and a zero ID is `InvalidValue` through Elisa and the raw ABI |
| 21–29 | Slot 0 registers as the center material with the authored factors, and again as a no-op. Slot 1 under the same ID conflicts and leaves it unchanged. Slot 2, the tile's slot 0 and an unregistered mesh are refused, and nothing is left registered |
| 31–34 | A requested mesh that hasn't been pumped is `AssetPending` for counting, one slot and a whole mesh, and leaves no material |
| 41–47 | The panel registers `[center, edge]` as a set, and again as a no-op. The glass panel registers `[glass, edge]`, reusing the panel's edge material. Glass holds alpha 0.5, blend and single-sided |
| 51–68 | Each case leaves no new material or set: a 1- or 3-material list, the tile, an unregistered mesh, an edge slot conflicting with the center material, one ID for both slots, a set ID that is a material's, a live set with other materials, a slot ID that is a set's, and a raw count that disagrees with the staging. The rollback keeps the center material it didn't create, and the live panel set still takes its own materials |
| 71–78 | In an open transaction the raw set call, one slot and a whole mesh are `BatchActive` while counting still works. The refused call consumed the staging, so after the abort the same raw call is `InvalidValue` |
| 81–90 | One commit stages a visible panel row with the panel set and a hidden glass row with the glass set. Both draw their subsets with their slots' materials. The frame shows a blue center strip between red ones, the glass row casts a shadow through its opaque edges, and the set and a listed material can't be unregistered while in use |
| 101–110 | Every set, material and mesh unregisters, and the shared-mesh, instance and set counts return to where they started |

## Mutation checks

The render mutations were applied to a copy of `native/`, and the smoke host
was rebuilt from it and linked against the smoke's Elisa archive. The eleven
mutations from the material-subset slice still fail group 193 at their
earlier cases. An unmutated control exited 0. Every new mutant failed at the
listed case:

| Mutation | Failing case |
| --- | --- |
| a failed slot leaves earlier slots' materials registered | 196/65: a material from a refused call is still registered |
| a failed set leaves the slots' materials registered | 196/65 |
| rollback also removes materials the call didn't create | 196/55: the center material is gone |
| no check of the list against the cooked count | 196/51: a 1-material list registers on the panel |
| whole-mesh registration runs inside a transaction | 196/75 |
| the whole-mesh call keeps the staging | 196/77: the raw call after the abort registers |
| single-slot registration runs inside a transaction | 196/75 |
| single-slot registration reads slot 0 | 196/24: slot 1 under the center ID is a no-op |
| cooked emission dropped | 196/22 |
| cooked sidedness dropped | 196/22 |
| cooked alpha mode read as opaque | 196/46: glass isn't blended |
| a loading mesh reads as failed | 196/33 |
| a material ID's conflicting factors accepted | 196/24 |
| a material may take a set's ID | 193/23: the shared check also guards the plain material ABI |
| registration doesn't report the material it created | 196/65 |

The ten slot-material loader mutations ran through the sanitized loader test.
The control passed all 44 cases, and each mutant failed the listed cases:

| Mutation | Failing cases |
| --- | --- |
| no factor range check | above one, negative, NaN, infinite, cutoff |
| no upper bound | above one, infinite, cutoff |
| mask mode allowed | mask |
| flags unchecked | flags |
| one key alone reads as no records | no stride, no records |
| records allowed without `material_slots` | without subsets |
| any stride | stride 44 |
| cutoff not read | material bounds |
| emissive read one factor early | the panel `.pkg` and `.elpk`, two records, single material |
| records ignored | all 20 cases with slot materials |

The earlier ten subset mutations still fail as before.

Thirteen cooker mutations ran on a copy of `scripts/` through
`cook_gltf_asset.py --self-test`. Each one failed it:
- textures allowed
- unknown keys allowed
- `MASK` cooked as opaque
- any alpha mode
- any `doubleSided` value
- no range check
- booleans accepted as factors
- any list length
- a non-object `pbrMetallicRoughness`
- a metallic default of 0
- a roughness default of 0.5
- flags dropped
- records omitted

The control passed.

## Limits

- **No textures.** Materials with textures still fail in the cooker. The game
  registers those slots at runtime with `register_snapshot_material_asset`
  and texture IDs. Because a mask needs the base-color texture's alpha,
  `MASK` fails too. (Since superseded: textures and masks now cook, as
  [`cooked-material-textures.md`](cooked-material-textures.md) describes.)
- **Unit emission.** glTF `emissiveFactor` is at most 1, and
  `KHR_materials_emissive_strength` is an extension, which fails. Cooked
  emission uses the runtime's default strength.
- **Runtime package, not cooker.** Slot records and subset bindings are valid
  on skinned runtime packages; the glTF cooker still needs its complete
  multi-material skinned scene path.
- **IDs come from the game.** A cooked slot has no stable asset ID of its
  own. The game picks the material and set IDs, and registering a whole mesh
  twice under different IDs creates duplicate Wicked materials.

## Validation on 2026-09-21

These ran in a detached worktree on 0d17016 that held only this change,
because other sessions shared the main working tree.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal twice, before
  and after case 55 gained its center-material check. That covers groups
  193–196, the loader's 44 cases, the earlier snapshot, bundle and
  asynchronous asset tests, the maze snapshot test, the maze application
  smoke and the packaged maze cases.
- `cook_gltf_asset.py --self-test` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17 and 6 obligations, none failed).
- The render, loader and cooker mutations above failed as listed, and all
  three controls passed.
- A syntax-only compile of `native/render_scene_abi.cpp` without
  `ELISA_RENDER_SCENE_TEST_PROBE` passed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
