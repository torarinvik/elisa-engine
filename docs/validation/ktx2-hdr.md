# HDR Basis KTX2 upload

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, Wicked Engine 0.72.114,
and pinned Basis Universal 99f52d63.

The runtime detects HDR KTX2 content before selecting a GPU format. It uploads
unsigned BC6H when the device accepts that format; otherwise it transcodes to
linear `R16G16B16A16_FLOAT`. HDR with alpha requires the half-float path, and
HDR normal data or devices without either range-preserving format fail instead
of falling back to 8-bit RGBA. The selected encoding is queried from Wicked's
device by creating a bounded probe texture.

`scripts/basisu_probe.py` generates a deterministic 4x4 RGBE source with values
above 1.0, encodes it as UASTC HDR KTX2, then checks its HDR/linear metadata,
BC6H transcode, and RGBA16F transcode. The half-float samples retain values
above 1.0. The bounded image-package validator accepts only the matching
UASTC HDR 4x4 Vulkan format, DFD profile, block shape, linear transfer, and Zstd
supercompression; malformed vkFormat/DFD combinations fail. The Wicked texture
probe uploads the same fixture and verifies the actual GPU format against
device support. Policy checks cover BC6H preference, half-float fallback,
alpha preservation, and rejection when only 8-bit formats are available.

## Validation

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3 scripts/basisu_probe.py`
  passed and reported `bc6h=1 rgba16f_dynamic_range=1`.
```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 \
PYTHON_BIN=/opt/homebrew/bin/python3 \
"/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/elisa-script/.validation/native/elisascript-current-rebuilt" \
scripts/wicked_probe.elisascript texture
```

This passed on the Metal device and uploaded the HDR fixture as BC6H.
- `scripts/wicked_probe.elisascript texture` now runs the CPU Basis fixture
  generation/transcode test itself, so a clean build does not rely on a
  pre-existing ignored HDR file.
- `scripts/cook_gltf_asset.py --self-test` passed with both valid and malformed
  UASTC HDR profile checks at the image-package boundary.

The GPU run exercised BC6H on this Mac. The RGBA16F device fallback is covered
by policy tests but was not selected by this device; other GPU backends remain
unverified. The fixture is single-mip and 2D. HDR environment lighting,
lightmap authoring, quality comparisons, and broader compressed/HDR formats
remain open A06 work.
