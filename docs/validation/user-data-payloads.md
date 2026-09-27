# User-data payload records

**Date:** 2026-09-27

The compact `UserData::write_blob` format holds at most sixteen `i64` fields.
`UserData::write_payload` and `read_payload` add a separate caller-owned byte
record for larger application schemas. The native service stores each record
under a validated key in the application user-data directory, with a 1 MiB
payload ceiling.

The `.data` envelope contains a magic value, envelope version, caller schema
version, payload length, reserved zero field, payload bytes, and an FNV-1a
checksum over the header and payload. Reads reject invalid lengths, unknown
envelope versions, checksum failures, and truncated or oversized files before
copying into the caller's buffer. If the buffer is too small, the API returns
`UserDataError.Capacity` without changing its bytes or output version/length.
Successful reads clear the unused buffer tail. Writes stage a temporary file
and atomically rename it over the previous record; the byte API does not yet
provide the fsynced journal and crash-recovery protocol used by
`scripts/save_journal.py`.

Validation is part of `scripts/check.elisascript`. The native service test
checks binary values including zero and high-bit bytes, replacement, invalid
sizes, traversal rejection, missing data, undersized buffers, checksum
corruption, and removal. The Elisa application smoke exercises the same
round-trip and verifies that a rejected short-buffer read leaves caller state
unchanged.

This is a storage primitive, not the runtime world-save codec. `Save::Blob`
remains the small structured format, and no `PrefabScene::SceneSnapshot` or
whole-world record is encoded through this byte channel yet. Snapshot format,
migrations, loading rollback, and native resource rehydration remain open W06
work.
