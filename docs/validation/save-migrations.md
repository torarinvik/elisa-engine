# Savegame migration chains

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler. This is W06 progress: the runtime-schema migration chain that
earlier notes listed as remaining.

## Design

`src/world/save_migrations.elisa` (`SaveMigrations`) describes schema
upgrades as data. Step *n* upgrades version *n* to *n + 1* with one
operation: `AddField` (insert with a default), `Append`, `DropField` or
`Scale`.

- `check` validates a chain before use: it must be non-empty, have at most
  16 steps, name fields below 8, and use scale factors in 1..1,000,000.
- `migrate` runs only the steps after the save's version. It reports
  `Current`, `TooNew` (a save from a newer build), `TooOld`, `BadChain`,
  `Capacity` (a missing field or a full record) or `Overflow`.
- It works on a copy. On any failure the original values come back
  unchanged, so a half-upgraded record never exists.

## Checks

- `test/save_migrations.elisa` exits 0. It covers:
  - a full v1-to-v5 upgrade and a mid-chain v3 save;
  - current, newer and older saves;
  - overflow in step 2 returning the untouched original;
  - a narrow record and a full record (`Capacity`);
  - empty, bad-scale and bad-field chains;
  - the step cap.
- Negative control: returning the partially migrated copy on failure makes
  the test exit 8.
- Wired into `scripts/check.elisascript`.

## Adoption

`SaveSchema::migrate` now runs its v1-to-v2 upgrade (append one zeroed
field) through this chain, so a failed record is left as it was.
`test/save_schema.elisa` still exits 0. Negative control: appending 7
instead of 0 makes it exit 7. `test/save_migrations.elisa` adds `Append`
cases (codes 16-17).

## Gaps

- The course checkpoint still uses its own hand-written v1-to-v2 step.
- Whole-world restoration and native resource rehydration remain.
