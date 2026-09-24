# Native PBR material bridge

`native/pbr_material_bridge.h` maps a validated Elisa PBR descriptor to a
generation-checked Wicked material handle. Base color, emissive color and
strength, metalness, roughness, alpha mode and cutoff, double-sidedness, and
shadow casting are updated through Wicked's dirty-marked material setters.

The bridge rejects non-finite colors, out-of-range normalized colors, invalid
metalness, and roughness below the supported minimum. Destroying the handle
removes its backing object and returns the scene object count to its baseline;
foreign handles cannot update another bridge.

The native gate exercises create, update, mask and blend alpha policy,
base-color texture-slot binding, validation, foreign-owner rejection, and
unload. The authored goal material consumes a real Basis/KTX2 GPU resource
(with the KTX1 upload retained as a structural probe). Authored glTF material
factors and base-color, normal, metallic-roughness, emissive and occlusion
images are cooked into mesh bundles, registered against Wicked materials,
and rendered by the SDL3/Metal smoke. See
[`cooked-material-textures.md`](cooked-material-textures.md) and
[`gltf-mikktspace.md`](gltf-mikktspace.md) for the fixture and tangent evidence.
