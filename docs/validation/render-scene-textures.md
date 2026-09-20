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

The smoke assigns the tracked PNG fixture to base-color and normal slots,
rejects traversal, absolute, and symlink-escape paths, then renders a cooked
FBX mesh and checks handle cleanup. Wicked loads and block-compresses the
fixture as BC3. The pixel probe confirms that the cooked mesh remains visible;
it does not compare texture samples against a reference image.

The cooked mesh format retains UVs but has no tangent stream, so normal maps
are assigned but are not ready for reliable shading. Tangent generation,
authored channel/color captures, full material descriptors, and asynchronous
texture residency remain open work.
