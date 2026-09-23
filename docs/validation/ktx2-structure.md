# KTX2 cooker-boundary validation

**Date:** 2026-09-23
**Scope:** Structural validation before bounded Basis KTX2 images are packed
into geometry or image bundles. The accepted layout follows the official
[Khronos KTX 2.0 file specification](https://registry.khronos.org/KTX/specs/2.0/ktxspec.v2.html).

## Checks and results

- `/opt/homebrew/bin/python3 scripts/cook_image_bundle.py --self-test` passed.
  It accepts metadata, BasisLZ global data, and multi-level section layouts;
  rejects 17 malformed DFD, KVD, SGD, level-index, type-size, and mip-overlap
  cases; and confirms deterministic bundle output.
- `/opt/homebrew/bin/python3 scripts/cook_gltf_asset.py --self-test` passed,
  including cooking the KTX2 material fixture into its bundle.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools /opt/homebrew/bin/python3
  scripts/basisu_probe.py` passed the CPU Basis color, cubemap, alpha, and
  normal-map transcodes. Its upload-shape policy checks accept ordinary 2D and
  square cubemap shapes while rejecting arrays, invalid face counts, and empty
  dimensions.
- `/opt/homebrew/bin/python3 scripts/test_geometry_subsets.py` passed with 104
  cases and zero failures.
- `scripts/check_module_hygiene.py`, `scripts/check_dependency_manifest.py`,
  and the full `elisascript scripts/check.elisascript` gate passed.

## Coverage limits

The Python boundary checks section bounds and ordering, DFD declared/block
sizes, KVD entry bounds, UTF-8 keys, sorting, uniqueness and padding, SGD
alignment and scheme, and non-overlapping mip ranges. They do not validate all
semantic DFD fields; the native Basis transcoder remains responsible for that
before decoding. The native Metal texture-upload assertions remain unverified:
the broader RenderScene smoke aborts with exit 134 after Wicked creates its
first 256 MiB GPU buffer, before it reaches texture upload. HDR and additional
GPU-compressed output formats remain unsupported by this change.
