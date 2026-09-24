# Cooked material textures

Cooked slot materials ([`cooked-slot-materials.md`](cooked-slot-materials.md))
carried only glTF factors. A textured material or an alpha mask failed in the
cooker, so a game rebuilt those slots by hand from separately registered
textures. Now the cooker writes each sampled image into the mesh's `.elpk`
bundle and records which images each slot samples. The game registers the
mesh's images and then its materials by ID, and the runtime binds each slot
to exactly those images.

## Path

1. **Cook.** `scripts/cook_gltf_textures.py` resolves each material's
   textures into eight runtime slots: base color, normal, surface
   (metallic-roughness), emissive, occlusion, clearcoat, clearcoat roughness,
   and clearcoat normal. Occlusion remains a separate material binding rather
   than a slot-texture reference.
   - **Texture info.** It may name `index`, `texCoord`, and `scale` for the
     normal texture or `strength` for occlusion.
     - `texCoord` must be the integer 0.
     - Normal `scale` is preserved within Wicked's finite half-float range
       (−65504 to 65504); non-finite and out-of-range values fail. Occlusion
       `strength` is preserved in [0, 1]; invalid and non-finite values fail.
     - Anything else fails as an unsupported property, including
       `KHR_texture_transform`.
   - **Textures.** A texture may name `source`, `sampler`, `name`, and the
     `KHR_texture_basisu` extension. Other texture extensions fail.
   - **Samplers.** A sampler must filter trilinearly and repeat on both axes.
     That is how the render scene samples every texture. It may omit any of
     those keys, but a string or a float fails.
   - **Occlusion.** `occlusionTexture` may reuse the metallic-roughness image
     or name a separate image. The latter is bound to Wicked's native
     `OCCLUSIONMAP` slot. The slot then sets flag bit 1, occlusion.
   - **Clearcoat.** `KHR_materials_clearcoat` supports its factor, roughness
     factor, and three texture infos. `clearcoatNormalTexture.scale` is
     preserved within Wicked's finite half-float range. The extension must be
     listed in `extensionsUsed`; unsupported properties and texture transforms
     fail instead of being dropped.
   - **Images.** PNG/JPEG and Basis KTX2 images must be embedded in a packed
     bufferView or a base64 data URI. KTX2 must be selected by
     `KHR_texture_basisu`; a PNG/JPEG core `source` fallback is omitted from
     the runtime bundle. KTX2 without a fallback requires the extension in
     both `extensionsUsed` and `extensionsRequired`.
     - Its bytes must match its `mimeType`, and its dimensions must decode.
     - A file URI or a remote URI fails.
     - A data URI that contradicts its `mimeType` fails.
     - A strided view, a view in another buffer, and a view past the buffer
       fail.
   - **Nothing unused.** Every declared texture and sampler must be used by a
     material. Every image must be sampled or serve as a KTX2 texture's
     PNG/JPEG fallback.
   - **Masks and UVs.** `MASK` is now allowed, but only with a base-color
     texture. A primitive whose material is textured needs `TEXCOORD_0`.
2. **Package.** Beside the slot material records, a textured mesh holds:
   - when any material uses a non-default normal scale,
     `slot_normal_scale_stride=4` and `slot_normal_scales_b64`: one
     little-endian `f32` per material slot. Older packages omit this sidecar
     and retain the default scale of 1.
   - when any material uses non-default AO strength,
     `slot_occlusion_strength_stride=4` and
     `slot_occlusion_strengths_b64`: one little-endian `f32` per slot. Older
     packages default to strength 1.
   - when any material uses non-default clearcoat values,
     `slot_clearcoat_factor_stride=12` and `slot_clearcoat_factors_b64`: three
     little-endian `f32` values per slot in factor, roughness, normal-scale
     order. Its absence means the default clearcoat values.
   - `texture_count`: 1–64 images
   - `texture_names_b64`: the image section names, `image_<glTF index>` in
     glTF image order
   - `slot_texture_stride=32` and `slot_textures_b64`: eight little-endian
     `u32` references per slot, each 0 for none or one more than the image's
     position in the names. The loader continues to accept the legacy stride
     20 format with its original five references.

   `cook_gltf_asset.py` writes each image as a section of the output `.elpk`.
   A `--texture` section that reuses one of those names fails. A textured
   source can't cook into a loose `.pkg`, unless a test asks for one with
   `allow_textures=True`.
3. **Load.** `native/cooked_slot_materials.h` now holds the slot material
   parser and the new `detail::parse_slot_textures`.
   - The optional normal-scale sidecar must contain one finite in-range value
     for every material record. Its absence keeps the glTF default of 1.
   - The optional AO-strength sidecar has one finite [0, 1] value per record;
     its absence keeps the glTF default of 1.
   - The optional clearcoat sidecar has three values per record: factor and
     roughness in [0, 1], then a finite normal scale within ±65504. Its absence
     keeps the no-clearcoat defaults.
   - Each optional sidecar has a stride key and a data key; its pair must be
     complete and requires slot material records.
   - Texture stride must be 20 or 32, and records must fill exactly one per
     slot. Clearcoat factors require stride 12 and one triple per slot.
   - **Names.** Each name must be a valid bundle section name: 1–15 bytes of
     `[a-z0-9_]`, with no embedded NUL. It can't be `mesh` or `manifest`, and
     can't repeat.
   - **References.** Every reference must name a listed image, and every
     listed image must be referenced.
   - **Needed images.** A masked slot needs a base-color image, and an
     occluded slot needs a surface or dedicated occlusion image. Flag bit 1
     is now allowed; other bits still fail.
   - **Bundles.** `load_cooked_geometry_asset` rejects slot textures in a text
     package. In an `.elpk`, each named section must exist, and its checksum
     is kept with the mesh.
4. **Count.** `RenderScene::snapshot_mesh_texture_count(mesh)` returns how
   many images a resident mesh's slots sample. Its errors match
   `snapshot_mesh_material_count`.
5. **Register an image.**
   `RenderScene::register_snapshot_mesh_texture(mesh, index, texture)`
   registers image `index` from the bundle the mesh loaded from.
   - It follows the bundle texture rules: the same image under the same ID
     is a no-op, a different image under that ID is `InvalidValue`, and an
     ID with a pending asynchronous request is `AssetPending`.
   - An index past the count is `InvalidValue`, and so is a mesh without
     images.
   - It can't run in a snapshot transaction (`BatchActive`).
   - If the section's checksum changed since the mesh loaded, the call is
     `AssetLoadFailure`, and stderr names the rewritten bundle.
   - Bundle texture registration and this call share
     `register_snapshot_bundle_texture_locked`.
6. **Register materials.** `register_snapshot_mesh_material` and
   `register_snapshot_mesh_materials` resolve each slot's images before they
   create any material.
   - A slot's image must be registered from the mesh's own bundle, under
     exactly one texture ID.
   - A missing image is `AssetLoadFailure`. So is the same section from
     another bundle, which doesn't count.
   - A second ID for the image is `InvalidValue`, because the slot would be
     ambiguous.
   - The Wicked material takes those textures in their slots, including a
     dedicated `OCCLUSIONMAP` when the glTF source uses one, and calls
     `SetOcclusionEnabled_Primary` for occluded slots.
   - Clearcoat factors and maps are copied to Wicked's clearcoat material
     fields and texture slots. The clearcoat-normal scale reaches both Wicked
     shader paths through the previously unused second per-material padding
     half; a zero clearcoat factor still leaves the material on its ordinary
     PBR path unless another clearcoat value or map is authored.
   - Occlusion is now part of the material's registration values. The same
     factors and images without occlusion are therefore a conflicting
     material.
   - Materials already block unregistering a texture they use.

## Hand-registered occlusion

Added on 2026-09-22. `RenderScene::register_snapshot_material_asset` can now
enable the occlusion that cooked slots get.
- **Elisa.** Naming `Material.occlusion` enables occlusion.
  - It must be the same texture as `metallic_roughness`.
  - If it is the only one of the two, it becomes the surface texture.
  - Two different textures are still `InvalidValue`.
- **ABI.** The wrapper calls
  `elisa_render_scene_v1_register_snapshot_material_asset_with_occlusion`.
  - It takes the `_with_textures` arguments plus an occlusion flag, 0 or 1.
  - `_with_textures` and the untextured form keep their signatures. They
    forward with the flag at 0.
- **Surface required.** Occlusion needs a surface texture.
  `snapshot_material_values_valid` now checks this for hand and cooked
  materials. The loader already rejected cooked occlusion without a surface
  image.
- **Matching.** Occlusion is still part of `snapshot_material_values_match`.
  - A hand material with the painted slot's factors, images and occlusion is
    the same registration as the slot.
  - The same material without occlusion still conflicts.

## Clearcoat materials

Added on 2026-09-24. Cooked glTF clearcoat and hand-registered snapshot
materials share the same eight-slot runtime representation.
- Cooked materials preserve clearcoat factor, roughness, normal-map scale, and
  their three texture references. Older five-slot package records still load.
- `Material::Material()` initializes clearcoat factors and maps to the glTF
  defaults. `set_texture` covers each clearcoat slot, and
  `set_clearcoat_factors` validates both unit factors and Wicked's half-float
  normal-scale range.
- The additive `_with_pbr_extensions` ABI carries clearcoat data while older
  material-registration entry points retain their signatures and default to
  no clearcoat.
- Registration matching includes the new clearcoat values and texture IDs, so
  two material IDs cannot alias different clearcoat states. The native smoke
  probes both cooked and hand-registered material paths, including the shader
  type, packed factors, texture bindings, and normal-map scale.
- The sanitized geometry loader accepts the eight-slot clearcoat bundle and
  rejects incomplete factor sidecars, invalid factors, out-of-range normal
  scales, and unsupported texture transforms. The full loader gate now runs
  162 cases with no failures.
- `test/render_scene_clearcoat_native.elisa` runs as SDL3/Metal smoke group 233.
  It registers all four bundle images, checks the three clearcoat map bindings
  and dimensions, verifies Wicked's clearcoat shader fields, then repeats the
  checks for a hand-registered material and releases each resource. The
  complete `scripts/render_scene_native_smoke.py` run passed, including the
  existing render comparisons and packaged-maze checks. This clearcoat case
  verifies the data path; a dedicated visible clearcoat reference comparison
  remains open under R04.

## Evidence

`test/fixtures/textured_panel.gltf` is written by
`scripts/gltf_texture_self_test.py --write-fixture`. It draws three
up-facing strips and a backdrop:

| Slot | Material | Images | What it shows |
| --- | --- | --- | --- |
| 0 | `cutout`: white, masked at 0.5, double-sided | a 32×32 data-URI base color, opaque green on its left half, clear on its right | green, then the backdrop through the clear half |
| 1 | `painted`: lit, metallic 1, roughness 1 | a 16×16 base color, red then blue; an 8×8 flat normal map; a 4×4 surface image that is also its occlusion | red, then blue |
| 2 | `glow`: black, emissive (1, 1, 1) | the painted base color, through a second glTF texture, as emission | red, then blue |
| 3 | `backdrop`: black, emissive red | none | red, under the cutout |

**Cooker.** `cook_gltf_asset.py --self-test` checks the fixture's exact slot
records, image references, sections, subsets and package lines. It also
checks:
- the bundle cooks twice to identical bytes, with a mesh section and the
  four image sections
- a loose `.pkg` fails, and so does a `--texture` that clashes with an image
  section
- an untextured variant cooks no texture keys

Twelve variants must cook the expected references:
- the base-color image also sampled for emission
- a data URI that names its `mimeType`
- a sampler with only a name
- default `scale` and `strength`
- positive and negative non-default normal scales
- a non-default occlusion strength
- an explicit `texCoord` 0

Sixty-two variants must fail for the stated reason. They cover:
- **UV sets:** a second UV set, or a boolean `texCoord`
- **Texture info:** a texture transform, a scaled base-color texture,
  string, boolean, non-finite or out-of-range normal scales, and string,
  boolean, non-finite or out-of-range occlusion strengths
- **Occlusion:** occlusion from another image, or without a surface image
- **Indices:** texture indices out of range, negative or strings; a
  non-object texture info; texture sources missing, out of range or negative
- **Textures and samplers:** texture extensions, sampler extras, a sampler
  out of range, and nearest, bilinear, clamped, string or float filters
- **Unused declarations:** an unused texture, sampler or image, and textures
  that no material samples
- **Image URIs:** an image file, a remote URI ending in `;base64,`, a
  bufferView beside a URI, a missing, GIF or contradicting `mimeType`, PNG
  bytes labeled JPEG, invalid base64, a data URI without an image, and a
  truncated PNG
- **Image views:** strided views, views in another buffer, views past the
  buffer and views out of range; image extras; non-list textures
- **Primitives:** a textured strip without UVs
- **Masks:** a mask without a base-color image

The earlier subset self-test's texture rejections now fail as missing
textures, because its panel declares none.

**Loader.** `scripts/test_geometry_subsets.py` now runs 162 sanitized cases. An
accepted case can list each slot's image references and flags and each
section's name and checksum.
- **Accepted.**
  - The cooked `textured.elpk` loads the fixture's records, and each image
    section's checksum is the zlib CRC-32 of its bytes.
  - A hand-built bundle lists its images out of section order, next to an
    unlisted `spare` section.
- **Rejected:**
  - a textured `.pkg`
  - a bundle without one image section
  - a count of zero, a count that disagrees with the names, and 65 images
  - stride 12, and each key missing alone
  - records one slot short or one slot long
  - textures without slot materials
  - duplicate, `mesh`, `manifest`, empty, uppercase, `../albedo`,
    NUL-bearing and 16-byte names
  - a reference past the images, and an image no slot samples
  - a mask without a base-color image, and occlusion without a surface image
  - the older untextured mask case now needs a texture, and so does flag 2
    on an untextured slot; flags 4 are out of range

**Native.** `test/render_scene_cooked_texture_native.elisa` runs in the
SDL3/Metal smoke after the node hierarchy test, as group 230. The render
smoke cooks the fixture into `build/cooked/subsets/textured.elpk`, copies it
to `textured_rewrite.elpk`, and writes `textured_variant.elpk` with images 0
and 1 swapped.

The test uses these hooks:
- `elisa_render_scene_v1_test_snapshot_material_texture_id` and
  `elisa_render_scene_v1_test_snapshot_material_texture_size` check the ID
  and the Wicked texture's size in each slot.
- `elisa_render_scene_v1_test_snapshot_material_occlusion` reads Wicked's
  occlusion flag.
- `elisa_render_scene_v1_test_replace_cooked_file` rewrites a bundle under
  `build/cooked/`. It exists only in probe builds.

| Cases | What they check |
| --- | --- |
| 1–3 | The textured, untextured and to-be-rewritten meshes register |
| 11–14 | The textured mesh counts 4 images and the untextured panel 0. An unregistered mesh is `AssetLoadFailure`, and a zero ID is `InvalidValue` through Elisa and the raw ABI |
| 21–28 | With the variant bundle's `image_3` registered, the cutout slot and the whole mesh are still `AssetLoadFailure` and leave nothing registered. That ID can't take the mesh's own image. The untextured backdrop slot registers at once |
| 31–45 | Each image registers under its own ID, and a repeat is a no-op. Refused: one ID for two images, index 4, the untextured panel, an unregistered mesh, zero IDs, a call in an open transaction, and an ID an asynchronous request holds. None of them add texture memory |
| 51–60 | The whole mesh registers as a set. Each Wicked material holds its slot's factors, textures, texture sizes and occlusion. A repeat is a no-op. The glow slot can't take the painted material's ID. A hand-registered material with the painted slot's factors and images also names its surface image as occlusion. It enables occlusion, so the painted slot registers under its ID as a no-op |
| 111–120 | These run after 51–60. A hand material that names its surface image only as occlusion binds it as the surface image, with occlusion on. Naming it in both fields is the same registration. Without occlusion, the same ID conflicts. Under a new ID the material registers with occlusion off, and the painted slot conflicts with it. The older `_with_textures` form repeats that registration as a no-op. A separate occlusion image is `InvalidValue`. The raw ABI refuses occlusion without a surface image, and a flag of 2. Neither leaves a material behind |
| 61–66 | A second ID for `image_0` makes the painted and glow slots and the whole mesh `InvalidValue` and leaves nothing behind. The cutout, which doesn't sample it, still registers |
| 71–76 | After the rewrite copy is replaced by the variant, its changed `image_0` fails to load and adds no memory. Its unchanged `image_2` still registers |
| 81–87 | A committed row draws the set. The frame shows, from the right, the cutout's green, the backdrop's red through the clear half, the painted red and blue, and the glow's red and blue. The images in use can't be unregistered |
| 91–94, 100–101 | Everything unregisters, and shared meshes, instances, sets and texture memory return to where they started |

## Mutation checks

Thirteen render mutations were applied to a copy of `native/`. The smoke host
was rebuilt from each copy and linked against the smoke's Elisa archive. An
unmutated control exited 0, and twelve mutants failed:

| Mutation | Result |
| --- | --- |
| occluded slots don't enable Wicked occlusion | 53: the painted material has no occlusion |
| occlusion left out of the material comparison | 60: the hand material without occlusion matches the slot |
| slot image references ignored | 22: the cutout registers without its image |
| a second ID for an image allowed | 62: the extra ID is in use |
| an image from any bundle accepted | 22: the variant's `image_3` stands in |
| no checksum check | 72: the rewritten image loads |
| mesh images always read from section 0 | 34: image 1 under image 0's ID is a no-op |
| mesh image registration inside a transaction | 42 |
| the image count reads the slot count | 11 |
| bundle emissive maps dropped from Wicked materials | 54: the glow has no emissive texture |
| textures sampled at a half-texture offset for this test's materials | 85: only the frame catches it |
| bundle base-color and emissive maps swapped in Wicked | exit 170: bundle texture case 10, before group 230 runs |

A mutation that sampled every texture through Wicked's second UV set
survived, as expected. Wicked copies the first UV set into the second when a
mesh has only one, and every cooked mesh does. The first eleven rows above
failed group 229 before the group moved to 230. After the move, a control and
the checksum mutation reran and exited 0 and 230 at case 72.

Twenty loader mutations ran through the sanitized loader test. The
control passed all 74 cases.
- **Eighteen failed.** Each mutation below is paired with the cases that
  caught it:
  - flag bit 1 rejected: every occluded case
  - occlusion not read: the occluded accepts and occlusion without a surface
  - partial key sets read as none: the four missing-key cases
  - 65 images allowed: too many
  - any stride: stride
  - names unchecked: the seven bad names
  - duplicates allowed: duplicate
  - reserved names allowed: `mesh`, `manifest`
  - embedded NUL allowed: NUL
  - references off by one: beyond
  - unsampled images allowed: unsampled
  - a mask without a base image: both mask cases
  - occlusion without a surface image: both occlusion cases
  - references shifted by one slot: the textured accepts
  - textures in a text package: `textured.pkg`
  - a missing section allowed: `textured-missing.elpk`
  - checksums zeroed: both bundle accepts
  - texture records ignored: 29 cases
- **Two can't fail.** "No zero-count check" and "textures without slot
  materials" are equivalent mutations. `decode_base64` rejects empty text,
  so a zero count can't come with decodable names, and no records can't come
  with a decodable reference field. The guards stay as statements of the
  format.

Thirty-six cooker mutations ran on a copy of `scripts/` through
`cook_gltf_asset.py --self-test`. The first run left six alive. Each needed a
new rejection:
- a string filter only fails because `"9729" != 9729`, but `9729.0` passes
  without the type check
- boolean `true` passes a Python equality check against numeric scale 1
- negative texture indices pass an upper-bound-only check
- negative sources pass the same way
- a remote URI with `;base64,` passes without the `data:` check
- declared textures that no material samples need their own check

With the six cases added, all 36 mutants fail and the control passes. They
cover sampler, texture-info, texture and image validation, occlusion,
unused declarations, references, slot order, section names, masks, UVs,
loose packages, package lines and section clashes.

### Hand-registered occlusion mutations

Six more mutations tested the occlusion registration change on 2026-09-22.
- The five native mutants were applied to a copy of `native/` and linked
  against the smoke's Elisa archive.
- The wrapper mutant was applied to copies of `src/`, `test/` and
  `examples/`, and its Elisa archive was recompiled.

An unmutated control exited 0, and all six failed group 230:

| Mutation | Result |
| --- | --- |
| the ABI drops the occlusion flag | 59: the hand material has no occlusion |
| the wrapper never passes occlusion | 59 |
| occlusion allowed without a surface texture | 117: the raw call without a surface registers |
| flags other than 0 and 1 accepted | 118: flag 2 registers |
| the `_with_textures` form forwards occlusion | 115: the legacy call conflicts with the bare material |
| occlusion left out of the material comparison | 113: the hand material without occlusion matches |

The comparison mutation in the first table failed at 60 before this change.
Case 60 now expects the hand material with occlusion to match, so case 113
catches that mutation.

## Limits

- **Synchronous image registration.** `register_snapshot_mesh_texture` reads
  the bundle on the calling thread. A texture requested asynchronously from
  the same section doesn't satisfy a slot until it is resident, and a slot
  never waits for it: an unresolved image is `AssetLoadFailure`.
- **Embedded images only.** External image files, unsupported texture
  extensions such as `KHR_texture_transform`, second UV sets and normal scales
  outside Wicked's finite half-float range fail. Bundles retain PNG, JPEG or
  bounded 2D Basis KTX2 data; mip data stays in the KTX2 source.
- **Separate occlusion channel policy.** A separate glTF occlusion image is
  retained as a dedicated Wicked `OCCLUSIONMAP`; it is not packed into the
  metallic-roughness surface image. An occlusion texture named alone leaves
  metallic and roughness at their factors and does not act as a surface map.
- **Runtime package, not cooker.** Slot texture records and subset bindings are
  valid on skinned runtime packages; the glTF cooker still needs its complete
  multi-material skinned scene path.
- **IDs come from the game.** As with cooked factors, the game picks every
  texture, material and set ID.

## Validation on 2026-09-21

These ran in a detached worktree on e968abd that held only this change,
because other sessions shared the main working tree.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal twice with the
  group as 229, and again after it moved to 230. That covers:
  - groups 193–199, 227, 228 and 230
  - the loader's 74 cases
  - the maze application smoke and the packaged maze cases
- The rewrite case logged `Elisa bundle texture load failed: ...
  textured_rewrite.elpk changed since its mesh loaded`.
- `cook_gltf_asset.py --self-test` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17 and 6 obligations, none failed).
- The render, loader and cooker mutations above behaved as listed, and all
  three controls passed.
- A syntax-only compile of `native/render_scene_abi.cpp` without
  `ELISA_RENDER_SCENE_TEST_PROBE` passed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.

## Validation on 2026-09-22

The hand-registered occlusion change was tested in a detached worktree on
96dcd11 that held only this change, because other sessions shared the main
working tree.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal. That covers:
  - every render group, including 230 with cases 111–120
  - the loader's 74 cases
  - the maze application smoke and the packaged maze cases
- The six occlusion mutations above behaved as listed, and the control passed.
- A syntax-only compile of `native/render_scene_abi.cpp` without
  `ELISA_RENDER_SCENE_TEST_PROBE` passed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- `ELISA_ALLOW_STALE_STAGE1=1` used the sibling compiler's existing stage1
  binary.
- The full `scripts/check.elisascript` suite and the cooker self-test weren't
  rerun, because this change doesn't touch the cooker or the loader.

## Validation on 2026-09-24

The normal-scale and AO-strength sidecars, backend descriptor and Wicked
material application passed the glTF cooker self-test, the 150-case sanitized
geometry-loader test, and the Elisa material test. The SDL3/Metal render-scene
smoke confirmed both values reach Wicked's shader material. A separate dark AO
map at strength 0.65 rendered 0.0503 average luminance darkening against its
zero-strength capture; the mirrored-normal reference measured 0.1041 contrast
against a 0.04 image threshold. This visual check also exposed and fixed the
snapshot adapter's missing secondary-occlusion flag for dedicated AO maps.
