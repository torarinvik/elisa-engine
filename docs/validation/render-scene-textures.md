# Elisa render-scene texture assignment

`RenderScene::set_texture(handle, slot, asset_path)` loads an image through
Wicked's resource manager and assigns it to one live scene material. Supported
slots are base color, normal, packed surface, and emissive. The surface map
stores occlusion, roughness, metalness, and reflectance in RGBA order, as
consumed by the pinned Wicked shader. Paths are project-relative to
`ELISA_PROJECT_ROOT`; the shared resolver canonicalizes them and rejects
traversal, absolute paths, missing files, and symlink escapes. The material
retains Wicked's resource until the slot is replaced or the scene is destroyed.
All images request block compression, and normal images also use Wicked's
normal-map import path. Elisa selects the material shading model separately;
the API does not discover companion maps or convert FBX metadata.

Validation on macOS with SDL3/Metal and the configured Wicked libraries:

```sh
DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer \
  python3 scripts/render_scene_native_smoke.py
```

The native smoke passed on 2026-09-20. It assigned a tracked PNG fixture to
base color and a distinct copy to the normal slot, rejected traversal, absolute,
and symlink-escape paths, then rendered a cooked FBX mesh with emissive material
and checked handle cleanup. Wicked loaded and compressed the base-color fixture
as BC3, and the API accepted the distinct normal-slot assignment. The pixel
probe confirms the cooked mesh remains visible; it does not verify the normal
slot's final GPU format or compare texture samples against a reference image.

A hidden launch of the built game also loaded the actual 8192×8192 Arc Gate
base-color PNG. Wicked selected BC1 and logged a 32 MB raw block-texture
workspace, with no texture-load failure. This checks the project-rooted asset
path and decode/compression route; a visual capture is still needed to confirm
the map's appearance on the fence.

The same engine revision passed `python3 scripts/test_elisa_build_run.py`,
`elisascript scripts/check.elisascript`, `python3 scripts/check_source_length.py`,
and `python3 scripts/check_module_hygiene.py`. The game compiled through
`python3 ../amazing-labyrinth-engine/scripts/elisa_build_run.py build --project .`.

The cooked mesh format retains UVs but has no tangent stream, so normal maps
are assigned but not ready for reliable shading. FBX material discovery,
tangent generation, independent roughness/metallic map packing, authored
channel/color captures, and asynchronous texture residency remain open work.
