# KTX2 cooker-boundary validation

**Date:** 2026-09-23
**Scope:** Structural validation before bounded Basis KTX2 images are packed
into geometry or image bundles. The accepted layout follows the official
[Khronos KTX 2.0 file specification](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html).

## Checks and results

- `/opt/homebrew/bin/python3 scripts/cook_image_bundle.py --self-test` passed.
  It accepts metadata, BasisLZ global data, and multi-level section layouts;
  rejects 25 malformed DFD, KVD, SGD, level-index, type-size, scheme/profile,
  mip-order, alignment, padding, and trailing-data cases; and confirms
  deterministic bundle output.
- The boundary rejects impossible mip counts, mismatched UASTC/ETC1S
  supercompression schemes, wrong per-level UASTC block sizes, wrong physical
  mip order, unaligned or non-zero mip padding, and bytes after the final mip.
  Valid uncompressed UASTC levels use the KTX2-required block alignment; valid
  BasisLZ and Zstandard levels retain their scheme-defined byte alignment. A
  regression fixture also accepts the legacy supercompressed UASTC DFD with a
  zero bytes-per-plane field allowed by older KTX2 revisions.
- `/opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed,
  including cooking the KTX2 material fixture into its bundle.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3
  scripts/basisu_probe.py` passed the CPU Basis color, cubemap, alpha, and
  normal-map transcodes and the generated UASTC HDR 4x4 profile. The bounded
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

The Python boundary checks section bounds and ordering, supported scheme/DFD
pairs, UASTC block dimensions and per-mip byte counts, KVD entry bounds, UTF-8
keys, sorting, uniqueness and padding, SGD alignment, and exact mip layout.
It validates only the semantic DFD fields needed to admit the supported UASTC
HDR 4x4 profile; the native Basis transcoder remains responsible for other
payload semantics before decoding. The separate HDR note records real Metal
BC6H upload evidence and remaining hardware limits:
[`ktx2-hdr.md`](ktx2-hdr.md).
