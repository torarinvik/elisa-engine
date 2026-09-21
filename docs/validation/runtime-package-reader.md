# Runtime package reader validation

The native package boundary now reads a bounded section index before decoding
geometry. It caps total bytes, line and section sizes, and section count;
rejects duplicate keys and malformed lines; and validates the package source
path against absolute paths, backslashes, and `..` traversal. Numeric and
base64 decoding failures return an unloaded package instead of escaping into
the renderer. The same module now also parses a version-1 `ELPK` binary bundle
index with fixed entry sizes, 16-byte data alignment, duplicate-name and
non-overlap checks, supported compression values, and a 64 MiB unpacked-section
bound. Each entry's CRC-32 is checked against decoded section bytes for raw and
zstd storage; a mismatch clears the output and fails before device upload.

The SDL3/Metal native gate loads the real cooked maze package through this
reader twice and runs `native/package_bounds_probe.h` against duplicate,
traversal, malformed, missing-package, valid-binary-index, and overlapping
binary-section fixtures. The reader is the runtime side of the current text
package format and a reusable binary index boundary. `read_binary_package_section`
also decompresses bounded zstd sections and the gate verifies the decoded
payload. `resolve_package_path` selects an explicit override root before the
shipped base root, rejects traversal and invalid generations, and returns the
generation attached to the selected path. `native/virtual_file_service.h` adds
a bounded request table over that resolver: duplicate logical-name/section
requests coalesce, cancellation is explicit, reads complete in a caller-
supplied pump budget, and a remount invalidates queued work whose captured
generation is stale. Requests also carry a dependency generation token, which
is rejected before package allocation when it belongs to an older mount. The
probe covers override-backed zstd data, cancellation, generation invalidation,
stale dependency tokens, corrupted raw payloads, and symlink escapes from mount
roots. `pump_async` runs the same bounded pump on a worker future, while
dependency-aware requests reject missing, self, duplicate, and unsorted logical
package dependencies before queueing.
