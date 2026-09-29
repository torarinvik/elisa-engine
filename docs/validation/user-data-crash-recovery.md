# User-data crash recovery

Validated on 2026-09-29 on macOS 27.0 / Apple M5.

This is W06 progress. [`user-data-payloads.md`](user-data-payloads.md) said the
byte API had no flush or recovery protocol.

## Design (`native/user_data_abi.cpp`)

- **Durable write.** The staged temporary file is written and flushed to
  stable storage (`F_FULLFSYNC` on macOS, `fsync` elsewhere on POSIX,
  write-through move on Windows) before any rename. The directory is synced
  after the final rename.
- **Recovery copy.** Before a record is replaced, the previous record is
  renamed to `<key>.<ext>.bak`, but only if it passes its envelope checksum,
  so a damaged file never displaces a good backup.
- **Recovery read.** Blob and payload reads use the primary when it is
  present and checksummed. When it is missing, truncated or damaged, they
  use the backup, but only a fully checksummed backup. Otherwise the original
  error is reported, so a record with no good copy still gives `Corrupt` or
  `NotFound`. An I/O failure on the primary is reported, not masked.
- **Removal.** `remove_blob` and `remove_payload` delete the backup too, so
  a removed record cannot resurface.
- A crash between the two renames leaves only the backup, and the next read
  recovers it.

- **Orphan sweep.** `initialize` deletes `*.tmp-*` staging files older than
  an hour; newer ones stay, since another process may still be writing.

## Checks

- `test/user_data_service_test.cpp` codes 70–78: two generations round trip,
  a damaged primary recovers the previous generation, a missing primary
  recovers it, a damaged backup is never used (`NotFound`), removal deletes
  the backup, and a truncated payload recovers the previous payload.
- Codes 79–80: a three-hour-old orphan is swept at initialise and a fresh
  one is kept; skipping the sweep fails code 80.
- Negative control: making the read ignore the backup fails code 73.
- The existing corruption, truncation and wrong-version cases still pass,
  since they have no backup.
- The character-course smokes, which save and reload through this service,
  pass.

## Gaps

- Recovery gives the previous record, so up to one save is lost, by design.
- Whole-world restoration and native resource rehydration remain.
