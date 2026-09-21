# Asset catalogue validation

## Scope

The offline cooker now writes a versioned SQLite catalogue with assets,
embedded-source dependencies, diagnostics, and deterministic cook-cache rows.
SQLite WAL mode and an immediate transaction keep a failed cook from exposing a
partial update. The cache key combines stable source identity, content hash,
and settings hash, so duplicate requests converge on one artifact.

## Evidence

Run from the engine root:

```sh
python3 scripts/cook_assets.py --self-test
python3 scripts/cook_assets.py "$PWD"
python3 -m py_compile scripts/cook_assets.py scripts/record_validation_assets.py
```

The self-test passed with `7 crafted + 96 fuzzed documents rejected`. It also
rolled back a writer, reopened the database, issued two concurrent duplicate
requests, and required one ready cache row with schema `2`. The real cook
produced one asset row, one dependency, one diagnostic, and one ready cache row;
`record_validation_assets.py` checks those rows during the validation record.

### Edited sources

Every content version of a source writes the same artifact file. So when a
source is cooked again with new content, older cache rows for that source and
artifact are marked `stale`, and only the newest row stays `ready`. Before this
fix, adding texture coordinates to `maze_tile.gltf` left two `ready` rows in an
existing `build/catalogue.db`. The validation record then rejected the
catalogue. The self-test now records two contents in turn and requires exactly
one `ready` row after each. Without the `stale` update, the self-test fails.

## Boundary

This database is an editor/build-tool catalogue. Runtime packages still carry
their own bounded indexes and never depend on SQLite or filesystem paths.
