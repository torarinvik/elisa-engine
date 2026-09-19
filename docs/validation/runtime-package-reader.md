# Runtime package reader validation

The native package boundary now reads a bounded section index before decoding
geometry. It caps total bytes, line and section sizes, and section count;
rejects duplicate keys and malformed lines; and validates the package source
path against absolute paths, backslashes, and `..` traversal. Numeric and
base64 decoding failures return an unloaded package instead of escaping into
the renderer.

The SDL3/Metal native gate loads the real cooked maze package through this
reader twice and runs `native/package_bounds_probe.h` against duplicate,
traversal, malformed, and missing-package fixtures. The reader is the runtime
side of the current text package format. A zstd-indexed bundle, overrides, and
async reads remain the broader A03 follow-up.
