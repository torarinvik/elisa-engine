# Cooked image assets

Startup sampling of the labyrinth game showed the main thread spending half of
a 14 s scene load decoding PNG textures, several of them 4096 or 8192 pixels
wide, before any of them could upload. Source art keeps its authored size; the
runtime should decode a bounded copy.

## Path

1. **Declare.** A project bounds a loose texture with the `image` importer:

   ```json
   {"importer": "image", "source": "assets/crate.png",
    "output": "build/cooked/textures/crate.png", "max_size": 2048}
   ```

   `scripts/asset_cooks.py` (split out of the build runner) requires a `.png`,
   `.jpg` or `.jpeg` source, an output of the same type inside the project that
   is not the source, and an integer `max_size` from 1 to 8192. The importer
   takes no `asset_path`, `textures`, `dependencies` or geometry keys.
   A `glb` cook with `texture_output` may add `texture_max_size` with the same
   bound for the base-color image it extracts.
2. **Cook.** `scripts/cook_image_asset.py SOURCE --output PATH --max-size N`
   decodes the image with Pillow, rejects anything that is not a PNG or JPEG
   within 1–8192 pixels per side, and resamples it (Lanczos, aspect kept) only
   when a side exceeds the bound; a small image is copied byte for byte. PNG
   output keeps alpha. Pillow is a cook-time dependency only; without it the
   cook fails with an install hint and the runtime is unaffected.
3. **Load.** The game references the cooked copy under `build/cooked`, which the
   macOS packager already stages, so the bundle no longer needs the source
   texture directories.

## Checks

- `python3 scripts/cook_image_asset.py --self-test` — bounds an 8×4 fixture to
  4×2, copies an already-small image unchanged, and rejects a suffix change,
  a zero bound, a missing source and an output over the source.
- `python3 scripts/test_elisa_build_run.py` — the `image` importer forwards its
  source, output and bound, rejects malformed bounds, mismatched types, geometry
  keys and self-overwrites; `texture_max_size` reaches the GLB cooker and needs
  `texture_output`.
