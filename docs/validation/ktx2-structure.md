# KTX2 cooker-boundary validation

**Date:** 2026-09-24
**Scope:** Structural validation before bounded Basis KTX2 images are packed
into geometry or image bundles. The accepted layout follows the official
[Khronos KTX 2.0 file specification](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html).

## Checks and results

- `/opt/homebrew/bin/python3 scripts/cook_image_bundle.py --self-test` passed.
  It accepts metadata, one-sample opaque ETC1S, two-sample alpha and RG ETC1S,
  BasisLZ global data, multi-level layouts, and legacy supercompressed ETC1S
  and UASTC DFDs with zero bytes-per-plane fields. It rejects 37 malformed DFD, KVD, SGD,
  level-index, type-size, scheme/profile, mip-order, alignment, padding, and
  trailing-data cases, and confirms deterministic bundle output.
- The boundary rejects impossible mip counts, mismatched UASTC/ETC1S
  supercompression schemes, wrong per-level UASTC block sizes, wrong physical
  mip order, unaligned or non-zero mip padding, and bytes after the final mip.
  DFD validation checks the basic descriptor header, supported BT.709
  primaries and linear/sRGB transfer functions, straight-alpha policy, sample
  bit ranges and qualifiers, supported UASTC channel IDs, and the valid
  one- or two-sample ETC1S channel/plane layouts. Premultiplied alpha and
  unsupported color metadata are rejected until the runtime can preserve them.
  Zero bytes-per-plane fields are accepted for supercompressed ETC1S and
  Zstandard UASTC to preserve older KTX2 files; uncompressed UASTC must declare
  its 16-byte plane.
  Valid uncompressed UASTC levels use the KTX2-required block alignment; valid
  BasisLZ and Zstandard levels retain their scheme-defined byte alignment.
- `/opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed,
  including cooking the KTX2 material fixture into its bundle and adding,
  replacing, and revalidating sorted KTX2 key/value metadata.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3
  scripts/basisu_probe.py` passed the CPU Basis color, cubemap, alpha, and
  normal-map transcodes, channel-swizzle metadata, and the generated UASTC HDR
  4x4 profile. The bounded
  cooker admits that exact HDR vkFormat/DFD/block/transfer/supercompression
  combination and rejects a mismatched LDR DFD. Its upload-shape policy checks
  accept ordinary 2D and square cubemap shapes while rejecting arrays, invalid
  face counts, and empty dimensions.
- `/opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed with 112
  cases and zero failures.
- `/opt/homebrew/bin/python3 scripts/basisu_probe.py` generated real Basis UASTC
  LDR and UASTC HDR KTX2 outputs. `encoded_image_dimensions` accepted both as
  4x4 images; Basis CPU transcodes for color, cubemap, alpha, normal, and HDR
  passed. The separate HDR note records the existing Metal upload probe.
- `scripts/check_module_hygiene.py`, `scripts/check_dependency_manifest.py`,
  and the full `elisascript scripts/check.elisascript` gate passed.

## Coverage limits

The Python boundary checks supported Basis DFD profiles and sample records,
section bounds and ordering, UASTC block dimensions and per-mip byte counts,
KVD entry bounds, UTF-8 keys, sorting, uniqueness and padding, SGD alignment,
and exact mip layout. It does not decode the compressed payload; the native
Basis transcoder validates that content before upload. The separate HDR note
records real Metal BC6H upload evidence and remaining hardware limits:
[`ktx2-hdr.md`](ktx2-hdr.md).

## Channel swizzle upload

The runtime reads the standard [`KTXswizzle` metadata](https://github.khronos.org/KTX-Specification/ktxspec.v2.html#_ktxswizzle)
as a four-character mapping using `r`, `g`, `b`, `a`, `0`, or `1`. Identity
mappings keep the normal device format-selection path. Nonidentity mappings
use linear RGBA8, or RGBA16F for HDR, then remap each decoded mip before Wicked
uploads it. For sRGB color data, the fallback first converts RGB samples to
linear values and uploads to UNORM; this preserves the texture-sampling result
when a mapping moves alpha into a color channel. The native fixtures verify
`ra01` for normal X/Y split across red/alpha and `ar01` for alpha plus sRGB red;
Metal readback checks the reordered bytes and linearized red value. Other GPU
backends remain unverified.
