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

The engine cooker now derives a normalized tangent and bitangent sign for each
vertex after any mesh simplification. The `elisa-cooked-v2` reader accepts old
packages without tangents and validates the optional tangent stream when
present. `python3 scripts/cook_fbx_asset.py --self-test` passed with normalized,
orthogonal tangent frames and byte-identical repeated package output. The SDL3 /
Metal scene smoke passed with this package format and a normal-slot assignment.

A hidden launch of the built game also loaded the actual Arc Gate base-color and
normal paths from Elisa. Wicked selected BC1 for the 8192×8192 base-color PNG
and logged a 32 MB raw block-texture workspace, with no texture-load failure.
The fence package now includes tangent frames and increased from 619,538 to
833,098 bytes after cooking. This checks the project-rooted asset paths and
decode/compression route; a visual capture is still needed to confirm the maps'
appearance on the fence.

The same engine revision passed `python3 scripts/test_elisa_build_run.py`,
`elisascript scripts/check.elisascript`, `python3 scripts/check_source_length.py`,
and `python3 scripts/check_module_hygiene.py`. The game compiled through
`python3 ../amazing-labyrinth-engine/scripts/elisa_build_run.py build --project .`.

The cooked mesh format retains UVs and validated tangent frames generated from
the final simplified geometry, so Elisa can bind normal maps for Wicked's PBR
materials. FBX material discovery, independent roughness/metallic map packing, authored
channel/color captures, and asynchronous texture residency remain open work.
