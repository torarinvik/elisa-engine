# Native PBR material bridge

`native/pbr_material_bridge.h` maps a validated Elisa PBR descriptor to a
generation-checked Wicked material handle. Base color, emissive color and
strength, metalness, roughness, double-sidedness, and shadow casting are
updated through Wicked's dirty-marked material setters.

The bridge rejects non-finite colors, out-of-range normalized colors, invalid
metalness, and roughness below the supported minimum. Destroying the handle
removes its backing object and returns the scene object count to its baseline;
foreign handles cannot update another bridge.

The native gate exercises create, update, validation, foreign-owner rejection,
and unload. The authored goal material also consumes a real KTX1 GPU resource;
texture-slot selection, authored glTF material import, and transparent blend
policy remain follow-up R04 work.
