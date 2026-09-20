# Elisa render-scene texture assignment

`RenderScene::set_texture(handle, slot, asset_path)` loads an image through
Wicked's resource manager and assigns it to one live scene material. Supported
slots are base color, normal, packed surface, and emissive. The surface map
stores occlusion, roughness, metalness, and reflectance in RGBA order, as
consumed by the pinned Wicked shader. Paths are project-relative to
`ELISA_PROJECT_ROOT`; the shared resolver canonicalizes the
path and rejects traversal, absolute paths, missing files, and symlink escapes.
The material retains Wicked's resource until the slot is replaced or the scene
is destroyed. All images request block compression, and normal images also use
Wicked's normal-map import path. Elisa selects the material shading model
separately; the API does not discover companion maps or convert FBX metadata.

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
probe confirms the cooked mesh remains visible; it does not verify normal-map
samples against a reference image.

The engine cooker now derives a normalized tangent and bitangent sign for each
vertex after any mesh simplification. The `elisa-cooked-v2` reader accepts old
packages without tangents and validates the optional tangent stream when
present. `python3 scripts/cook_fbx_asset.py --self-test` covers normalized,
orthogonal tangent frames and byte-identical repeated output. On 2026-09-20,
`DEVELOPER_DIR="$(xcode-select -p)" python3 scripts/render_scene_native_smoke.py`
cooked the synthetic FBX triangle with tangents, loaded it through
`RenderScene::create_mesh`, rendered it with Wicked, and passed the path,
handle, and cleanup checks.

The sibling worktree records a hidden launch loading the actual Arc Gate
base-color and normal paths, with a tangent-bearing fence package of 833,098
bytes. Its asset tree is absent from this checkout, so that external-asset run
has not been independently repeated here. FBX material discovery, independent
roughness/metallic packing, authored channel/color captures, and asynchronous
texture residency remain open work.

The full `DEVELOPER_DIR="$(xcode-select -p)" ELISA_ALLOW_STALE_STAGE1=1
elisascript scripts/wicked_probe.elisascript` gate also passed after this
integration. It loaded the tangent-bearing cooked geometry and compared two
rendered native frames exactly; the application profile and checked-startup
smoke passed in the same gate. This verifies package compatibility and upload
through the production Metal path, but the current screenshot check still does
not compare normal-map samples against an authored reference.
