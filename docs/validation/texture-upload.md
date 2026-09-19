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
transcoder in `native/ktx2_upload.h`, transcodes mip level zero to RGBA8, and
assigns that real Wicked GPU texture to the authored goal material. The
container is bounded and malformed input fails before allocation. Mip-chain
upload, color-space/alpha policy, GPU-native format selection, and memory
budgets remain follow-up A06 work.
