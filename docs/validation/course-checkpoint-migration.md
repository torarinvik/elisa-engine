# Course checkpoint schema migration

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is W06 progress. [`course-checkpoints.md`](course-checkpoints.md) listed a
migration chain as remaining.

## Design

- `CourseProgress` now writes schema 2: a schema marker, the six gameplay
  fields (phase, x/y/z millimetres, crouch, attempts) and a checksum over
  them (polynomial mod 1000000007, negatives folded into range).
- Schema 1, what earlier releases wrote, has six bare fields. `decode` picks
  the schema from the blob's field count, so an older save loads and the
  result is marked `migrated`. The next save rewrites it as schema 2.
- Schema 2 is rejected when the marker is not 2 (an unknown future schema) or
  the checksum disagrees. That catches a corrupted value that is still inside
  the range checks (for example a flipped attempts counter).
- Both schemas then pass the same range validation, so a migrated record
  cannot bypass it.
- `encode_legacy` exists so tests can write what an older release wrote.

## Checks

- Self-test code 68: a legacy record loads with `migrated` set and its
  attempts intact; a schema-2 save loads with `migrated` clear.
- Self-test code 69: a schema-2 record with a changed attempts field, and one
  with schema 3, are both `Rejected`, leaving the live run untouched.
- Existing codes 60–67 (round trip, out-of-range phase, wrong shape, missing,
  three cycles) still hold, and the relaunch smoke reloads a schema-2 save
  from a separate process.

## Gaps

- Only one migration step exists (1 to 2); no journal-based crash recovery
  through the runtime byte API yet.
- The beacon scene payload has its own version and is not covered here.
