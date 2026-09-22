# Bundle-backed textures

A03 needs textures to load from cooked bundles, not only from loose image
files. PNG and JPEG images, plus bounded 2D Basis KTX2 sections, register as
snapshot texture assets. The native maze's wall material now takes its base
color from a 32×32 brick image in its tile bundle.

## Path

1. **Declare.** A project lists image sections under an asset cook's
   `textures` object, mapping each section name to a project file:

   ```json
   "textures": {"wallalbedo": "assets/maze_wall.png"}
   ```

   `scripts/elisa_build_run.py` accepts at most 16 sections per cook, and only
   with the `gltf` importer and an `.elpk` output. Section names are 1–15 bytes
   of `[a-z0-9_]`, and `mesh` and `manifest` are reserved. Each path must stay
   inside the project and exist.
2. **Cook.** `scripts/cook_gltf_asset.py --texture SECTION=PATH` copies each
   image into its own section. The cooker reads the PNG `IHDR` or the JPEG frame
   header (`SOFn`). It rejects anything else, and any image whose width or
   height is outside 1–8192.
3. **Register.** `RenderScene::register_snapshot_bundle_texture_asset(asset,
   bundle_path, section)` calls the native service, which:
   - rejects an invalid section name (`InvalidValue`)
   - resolves the bundle under the project root, with the same confinement as
     cooked meshes
   - reads the ELPK index, finds the section, and bounds its unpacked size
     (64 MiB) before reading it
   - decompresses the section if needed and checks its CRC-32
   - reads the image header again and applies format-specific shape checks:
     PNG/JPEG dimensions are 1–8192; KTX2 must be a bounded 2D texture up to
     4096×4096 with at most 16 mips
   - decodes PNG/JPEG to CPU pixels, but keeps KTX2 encoded until material
     upload; encoded source and decoded pixels each have a 256 MiB resident
     budget (`Capacity` beyond either budget)
4. **Upload.** PNG/JPEG material registration creates a Wicked texture from
   decoded pixels, creates its mip chain, and schedules block compression.
   KTX2 is transcoded on the owner thread to a queried native format when its
   material first uses it. Resources are cached per texture asset and material
   role, so normal-data and color formats do not collide. Async requests decode
   PNG/JPEG on their worker; KTX2 is read and checksummed there, then transcoded
   when the owner thread adopts it into a material. Failed image decode or
   KTX2 upload returns `AssetLoadFailure` without installing a material.

Registering the same ID again with the same bundle and section succeeds and
changes nothing. The same ID with a different bundle or section, or as a loose
image path, is `InvalidValue`. A texture that a live material uses can't be
unregistered.

## Evidence

`test/render_scene_bundle_texture_native.elisa` runs inside the SDL3/Metal
native smoke. `scripts/bundle_texture_fixtures.py` writes its fixtures to
`build/cooked` first. The main fixture bundle holds a 16×8 PNG `albedo`, a
24×12 JPEG `photo` made by `sips`, the 4×4 Basis KTX2 section `ktx`, and two
bad sections: `huge` and `truncated`.

| Case | Expected |
| --- | --- |
| register `albedo` | OK; source-byte accounting grows, and stays under 4096 |
| register the same ID, bundle and section again | OK; source-byte accounting is unchanged |
| the same ID with section `photo` | `InvalidValue` |
| the same ID as a loose image path to the same bundle | `InvalidValue` |
| register `photo` under a second ID | OK |
| a material using each texture | Wicked texture is 16×8 and 24×12 |
| a section the bundle doesn't have | `AssetLoadFailure` |
| a KTX2 section (`ktx`) used by a material | OK; Wicked texture is 4×4 |
| a PNG header claiming 65535×65535 (`huge`) | `AssetLoadFailure`, before any decode |
| a bundle copy with one byte of `albedo` flipped | `AssetLoadFailure`, CRC mismatch |
| a `..` path, an absolute path, a symlink to a valid copy outside the root | `AssetLoadFailure` |
| the maze bundle's `mesh` section | `AssetLoadFailure`, not an image |
| section names `Albedo`, empty, and 16 bytes | `InvalidValue` |
| source-byte accounting after all the rejections | unchanged |
| `truncated`: signature and `IHDR` intact, image data cut short | registration fails with `AssetLoadFailure`; no texture slot or bytes remain |
| unregister a texture a live material uses | `InvalidValue` |
| unregister, re-register the ID as `photo`, register a material | the material's texture is 24×12 |
| unregister everything | source and decoded-byte accounting return to 0 |

Other checks:

- `test/maze_rendering_native.elisa` requires the maze wall material's base
  color texture to be 32×32.
- `scripts/packaged_maze_smoke.py` also flips one byte in the staged bundle's
  `wallalbedo` section. The maze then fails asset registration (exit 17).
- `scripts/cook_gltf_asset.py --self-test` cooks the same texture bundle twice
  and requires identical bytes. It rejects an oversized image header, a
  non-image, the reserved `mesh` name, `Albedo`, a duplicate section, and a
  `.pkg` output.
- `scripts/test_elisa_build_run.py` checks that declared textures become
  sorted `--texture` flags. It rejects six bad declarations.
- The native smoke now cooks the maze through `elisa_build_run.py`'s
  `cook_declared_assets`, so the maze tests use the textures the project
  declares.

## Mutation checks

Each mutation was applied to a native source, the full native smoke was run,
and the source was restored.

| Mutation | Result |
| --- | --- |
| the 1–8192 dimension check always passes | the `huge` section registers; the native smoke exits 182 (test 22) |
| loose-path re-registration ignores whether the slot holds a bundle section | the loose path re-registration succeeds; exit 167 (test 7) |
| unregistering doesn't return the source-byte accounting | the count stays above 0 after cleanup; exit 192 (test 32) |

## Limits

- **Formats.** The runtime accepts PNG, JPEG, and 2D Basis KTX2 sections.
  Basis KTX2 is capped at 4096×4096 and 16 mip levels. The glTF cooker still
  accepts embedded PNG and JPEG images only; DDS remains unsupported.
- **Decode timing.** Async PNG/JPEG requests decode on the A04 worker. KTX2
  section IO, header validation and CRC checking run there; Basis transcode and
  GPU creation run on the owner thread when a material first uses it. The
  synchronous compatibility entrypoint decodes PNG/JPEG on the owner thread.
- **Memory.** KTX2 encoded bytes and PNG/JPEG decoded pixels remain with the
  texture asset for later role-specific resource creation. Source and decoded
  pixel budgets are each 256 MiB; their exact upper bounds aren't exercised by
  the smoke.
- **Header bound.** A PNG/JPEG header may claim 8192×8192, so its decoder can
  allocate about 256 MiB of RGBA pixels. KTX2 output is separately capped at
  64 MiB during Basis upload.
- **Checksum identity.** The checksum and section form the native texture
  resource label; asset-ID conflicts and rewritten-bundle checks cover the
  identity rules. ResourceManager name caching is not used for bundle images.
- **Importers.** glTF cooks and texture-only `images` cooks take textures. The
  cooker doesn't read texture references from the glTF source; the project
  declares the sections, and Elisa code assigns them to materials. The maze
  wall texture now lives in its own bundle, which the tile bundle declares as a
  dependency (`docs/validation/bundle-dependencies.md`).

## Validation on 2026-09-21

- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal, including the
  bundle-texture cases, the textured maze and all seven packaged maze cases.
- The three mutations above failed the native smoke with the listed exits.
- `scripts/cook_gltf_asset.py --self-test`, `scripts/test_elisa_build_run.py`
  (9 tests) and `scripts/cook_assets.py --self-test` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17/17 and 6/6) and the validation record. The suite first failed on the
  asset catalogue; see "Edited sources" in `docs/validation/asset-catalogue.md`.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- Other work was editing the sibling compiler checkout during these runs, so
  its sources were newer than its `bin/elisac-stage1`. Every run used that
  existing binary from 10:05 with `ELISA_ALLOW_STALE_STAGE1=1`. It is the same
  binary the earlier slices were validated with.
