# Native texture upload

The Wicked backend now consumes the cooked RGBA KTX1 container through
`load_ktx1_texture_resource()` in `native/texture_upload.h`. The adapter checks
the KTX identifier, endianness, RGBA8 format, one-face/one-mip layout, bounded
dimensions, exact payload size, and then creates a real `R8G8B8A8_UNORM`
shader-resource texture from the image bytes.

The native gate uploads the same KTX produced by the cooker and assigns the
result to the authored goal material. The existing raw package, RGB565, BC1,
and KTX structural probes remain alongside this GPU upload check.

The gate also loads the cooked KTX2 artifact through the pinned Basis
transcoder in `native/ktx2_upload.h`, transcodes every bounded 2D mip level to
RGBA8, preserves the KTX2 transfer function in Wicked's UNORM/SRGB format,
and assigns that real GPU texture to the authored goal material. The current
cooked fixture is explicitly linear so Godot can retain its compressed KTX2
path; an sRGB fixture remains a separate compatibility case. The
container and decoded GPU payload are both bounded, and malformed input fails
before allocation. Normal-map/alpha policy and broader GPU-native format
selection remain follow-up A06 work.
