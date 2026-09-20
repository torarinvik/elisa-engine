# Elisa render-scene texture assignment

`RenderScene::set_texture(handle, slot, asset_path)` loads an image through
Wicked's resource manager and assigns it to one live scene material. Supported
slots are base color, normal, packed surface, and emissive. Paths must be
relative to `ELISA_PROJECT_ROOT`; the resolver canonicalizes them and rejects
traversal, absolute paths, missing files, and symlink escapes. All images
request block compression, and normal images also request Wicked's normal-map
compression path. Elisa selects the material shading model separately.

Validation on macOS with SDL3/Metal and the configured Wicked libraries:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  python3 scripts/render_scene_native_smoke.py
```

The smoke passed on 2026-09-20. It assigned the tracked PNG fixture to base
color and normal slots, rejected traversal, absolute, and symlink-escape paths,
then rendered the cooked FBX mesh with emissive material and checked cleanup.
Wicked loaded and block-compressed the fixture as BC3. The smoke verifies
resource loading and assignment through the public Elisa API; its pixel probe
confirms the cooked mesh remains visible, not that a particular texture sample
matches a reference image.

On 2026-09-20, a hidden launch of the built game also loaded the real 8192×8192
Arc Gate base-color PNG. Wicked selected BC1 compression and logged a 32 MB raw
block-texture workspace; the game emitted no texture-load failure. This checks
the actual project-rooted path and decode/compression route, while a visual
capture is still needed to confirm the map's sampled appearance on the fence.

The same engine revision also passed `python3 scripts/test_elisa_build_run.py`,
`elisascript scripts/check.elisascript`, `python3 scripts/check_source_length.py`,
and `python3 scripts/check_module_hygiene.py`. The game project compiled through
`python3 ../amazing-labyrinth-engine/scripts/elisa_build_run.py build --project .`.

The game currently assigns the supplied Arc Gate base-color map from Elisa.
The companion normal map remains unassigned because cooked geometry does not
yet contain tangent frames. FBX-to-material map discovery, independent
roughness/metallic map packing, tangent generation, and visual reference
captures are still open work.
