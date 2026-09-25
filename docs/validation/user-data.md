# Elisa user-data storage

`src/runtime/user_data.elisa` exposes application-owned storage through the
engine's public Elisa bundle. Games initialize it with a stable application ID,
then read, write, or remove bounded `Save::Blob` values by safe key. The game
keeps field meanings and its own schema version in Elisa; the native service
does not expose filesystem or vendor-library types.

By default, the engine selects the platform's normal per-user application-data
directory and adds the application ID. An absolute `ELISA_USER_DATA_DIR` can
override the base directory for sandboxing and tests. Application IDs and keys
are limited to ASCII letters, digits, dots, underscores, and hyphens; path
separators and dot-only components are rejected. The directory accessor returns
a runtime-owned UTF-8 path valid until the next initialization.

Each file has a native format version, a positive Elisa record version, one to
up to sixteen signed 64-bit fields, and a checksum. Writes go to a same-directory
temporary file and atomically replace the prior record. Reads reject invalid
headers, unsupported native formats, malformed lengths, checksums, symlinks,
and undersized output buffers. Elisa reads stage into a temporary blob, so any
error leaves the caller's previous value unchanged. Errors distinguish missing
records, incompatible format versions, corruption, invalid inputs, and I/O
failures.

Validation:

- `test/user_data_service_test.cpp` checks isolated paths, initialization,
  invalid keys, missing records, signed-value round-trips, replacement,
  capacity errors, corruption, future format versions, and removal.
- `test/user_data_probe.elisa` exercises the public API from an Elisa-owned
  native application, including staged reads and expected error mapping.
- `test/quality_settings_native_main.elisa` runs the same public user-data path
  with the engine's versioned render-quality profile encoder and decoder.
- `python3 scripts/application_native_smoke.py` compiles and runs both native
  application probes and the standalone storage test.
