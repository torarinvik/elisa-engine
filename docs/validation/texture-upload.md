# Native texture upload

The Wicked backend now consumes the cooked RGBA KTX1 container through
`load_ktx1_texture_resource()` in `native/texture_upload.h`. The adapter checks
the KTX identifier, endianness, RGBA8 format, one-face/one-mip layout, bounded
dimensions, exact payload size, and then creates a real `R8G8B8A8_UNORM`
shader-resource texture from the image bytes.

The native gate uploads the same KTX produced by the cooker and assigns the
result to the authored goal material. The existing raw package, RGB565, BC1,
and KTX structural probes remain alongside this GPU upload check.

KTX2/Basis transcoding and queried GPU-native format selection remain separate
follow-up work; malformed or unsupported containers must continue to fail
before allocation.
