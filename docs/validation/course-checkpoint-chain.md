# Course checkpoint through the SaveMigrations chain

- `CourseProgress::checkpoint_chain()` declares the checkpoint's migration
  chain. Its single 1→2 step is an identity rescale, because schema 2 kept the
  six payload fields and only added the marker and checksum around them.
- `decode` now reads both schemas into `SaveMigrations::Values` and accepts a
  record only if `SaveMigrations::migrate` reports Migrated or Current at
  version `SCHEMA` with six fields. The checksum is then checked on the result.
- `SaveMigrations` helpers were renamed to `chain_version`, `check_chain` and
  `apply_migration_step`, so stage1 cannot bind a bare call to another module's
  `current` or `check`. `test/save_migrations.elisa` still passes.
- The course self-test (code 68) now also requires the chain to be valid and
  to end at `SCHEMA`. Both course smokes pass.
- Control: an invalid step (scale by 0) makes every load fail closed; the
  smoke fails with 61, the first save/reload round trip.
- Remaining for W06: whole-world restoration and native resource rehydration.
