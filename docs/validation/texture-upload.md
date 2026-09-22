# Native texture upload

The Wicked backend now consumes the cooked RGBA KTX1 container through
`load_ktx1_texture_resource()` in `native/texture_upload.h`. The adapter checks
the KTX identifier, endianness, RGBA8 format, one-face/one-mip layout, bounded
dimensions, exact payload size, and then creates a real `R8G8B8A8_UNORM`
shader-resource texture from the image bytes.

The native gate uploads the same KTX produced by the cooker and assigns the
result to the authored goal material. The existing raw package, RGB565, BC1,
and KTX structural probes remain alongside this GPU upload check.

Loose KTX2 project texture assets and KTX2 sections registered through
RenderScene load through the pinned Basis transcoder in `native/ktx2_upload.h`.
It transcodes bounded mip chains to queried BC1 (opaque), BC7/BC3 (alpha), or
RGBA8 formats, preserves the KTX2 transfer function in
Wicked's UNORM/SRGB formats, and assigns the color texture to the authored goal
material. Normal-data usage stays on channel-preserving linear RGBA8 until a
suitable two-channel GPU format is implemented. The opaque color fixture is
linear; the alpha fixture is sRGB, so both transfer paths are covered.

Container and upload-payload memory are capped at 64 MiB; empty and oversized
files are rejected before allocation, and each mip dimension and face is
validated before GPU allocation. Six-face textures use
Wicked's cube resource flag and slice-major subresource order, while Basis
transcodes every face at one mip before advancing to the next. The CPU probe
checks opaque BC1 and alpha BC7/BC3 Basis targets, verifies alpha and sRGB
metadata, and checks distinct colors on all cube faces. The Wicked `texture`
phase runs a focused upload smoke that verifies queried format selection,
normal-data preservation, alpha safety, and cube shape. The render-scene smoke
registers both loose and bundle KTX2 assets and samples them from Elisa
materials. Bundle cooking from glTF image sources still accepts PNG and JPEG.
