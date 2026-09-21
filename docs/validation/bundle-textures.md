# Bundle-backed textures

A03 needs textures to load from cooked bundles, not only from loose image
files. This slice stores PNG and JPEG images as extra sections of an ELPK
bundle and registers them as snapshot texture assets. The native maze's wall
material now takes its base color from a 32×32 brick image in its tile bundle.

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
   - reads the image header again and applies the same format and 1–8192
     dimension checks
   - keeps the encoded bytes, up to 256 MiB across all bundle textures
     (`Capacity` beyond that)
4. **Decode.** When a material that uses the texture registers, Wicked decodes
   the kept bytes from memory. The resource name is
   `<canonical bundle path>#<section>-<crc32>.<png|jpg>`. Wicked caches
   resources by name and picks the decoder from the extension, so the name
   changes whenever the section or its bytes change. If the decode fails, the
   material registration fails with `AssetLoadFailure`.

Registering the same ID again with the same bundle and section succeeds and
changes nothing. The same ID with a different bundle or section, or as a loose
image path, is `InvalidValue`. A texture that a live material uses can't be
unregistered.

## Evidence

`test/render_scene_bundle_texture_native.elisa` runs inside the SDL3/Metal
native smoke. `scripts/bundle_texture_fixtures.py` writes its fixtures to
`build/cooked` first. The main fixture bundle holds a 16×8 PNG `albedo`, a
24×12 JPEG `photo` made by `sips`, and three bad sections: `huge`, `truncated`
and `ktx`.

| Case | Expected |
| --- | --- |
| register `albedo` | OK; retained bytes grow, and stay under 4096 |
| register the same ID, bundle and section again | OK; retained bytes unchanged |
| the same ID with section `photo` | `InvalidValue` |
| the same ID as a loose image path to the same bundle | `InvalidValue` |
| register `photo` under a second ID | OK |
| a material using each texture | Wicked texture is 16×8 and 24×12 |
| a section the bundle doesn't have | `AssetLoadFailure` |
| a KTX2 section (`ktx`) | `AssetLoadFailure`, not a PNG or JPEG |
| a PNG header claiming 65535×65535 (`huge`) | `AssetLoadFailure`, before any decode |
| a bundle copy with one byte of `albedo` flipped | `AssetLoadFailure`, CRC mismatch |
| a `..` path, an absolute path, a symlink to a valid copy outside the root | `AssetLoadFailure` |
| the maze bundle's `mesh` section | `AssetLoadFailure`, not an image |
| section names `Albedo`, empty, and 16 bytes | `InvalidValue` |
| retained bytes after all the rejections | unchanged |
| `truncated`: signature and `IHDR` intact, image data cut short | registration OK; the material using it fails with `AssetLoadFailure` |
| unregister a texture a live material uses | `InvalidValue` |
| unregister, re-register the ID as `photo`, register a material | the material's texture is 24×12 |
| unregister everything | retained bytes return to 0 |

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
| the resource name drops the section and checksum | `truncated` reuses the cached `albedo` resource, so its material registers; exit 174 (test 14) |
| unregistering doesn't return the retained bytes | retained bytes stay above 0 after cleanup; exit 192 (test 32) |

## Limits

- **Formats.** Only PNG and JPEG sections load. KTX2, Basis and DDS sections
  are rejected. KTX2 upload exists in `native/ktx2_upload.h`, but it isn't
  connected to bundle sections. That is A06.
- **Decode timing.** Wicked decodes on the owner thread while the material
  registers, not through the A04 loader.
- **Memory.** Encoded bytes stay in memory for as long as the texture is
  registered, so later materials can use them. The 256 MiB cap isn't tested.
- **Header bound.** A valid header may still claim 8192×8192, so the decoder
  can allocate about 256 MiB of RGBA pixels.
- **Checksum in the name.** No test rewrites a bundle during a run, so the
  checksum part of the resource name is untested. The section part is covered:
  without it, `truncated` would reuse the cached `albedo` resource.
- **Importers.** Only glTF cooks take textures. The cooker doesn't read texture
  references from the glTF source; the project declares the sections, and Elisa
  code assigns them to materials.
- **Remaining A03 work.** Custom dependency declarations are still open.

## Validation on 2026-09-21

- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal, including the
  bundle-texture cases, the textured maze and all seven packaged maze cases.
- The four mutations above failed the native smoke with the listed exits.
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
