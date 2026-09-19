# Runtime package reader validation

The native package boundary now reads a bounded section index before decoding
geometry. It caps total bytes, line and section sizes, and section count;
rejects duplicate keys and malformed lines; and validates the package source
path against absolute paths, backslashes, and `..` traversal. Numeric and
base64 decoding failures return an unloaded package instead of escaping into
the renderer. The same module now also parses a version-1 `ELPK` binary bundle
index with fixed entry sizes, 16-byte data alignment, duplicate-name and
non-overlap checks, supported compression values, and a 64 MiB unpacked-section
bound.

The SDL3/Metal native gate loads the real cooked maze package through this
reader twice and runs `native/package_bounds_probe.h` against duplicate,
traversal, malformed, missing-package, valid-binary-index, and overlapping
binary-section fixtures. The reader is the runtime side of the current text
package format and a reusable binary index boundary. zstd section reads,
overrides, async reads, and generation-tagged dependencies remain the broader
A03/A04 follow-up.
