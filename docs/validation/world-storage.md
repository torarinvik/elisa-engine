# Dynamic world-storage validation

`WorldStorage::Catalog` keeps engine-owned actor and projectile columns next to
bounded game-defined records. Game-defined types register a fixed schema of one
to eight `i64` fields, share the catalogue's monotonic identity stream, and
compact dense rows after destruction. Spawn rejects unknown types, schema
mismatches, per-type capacity exhaustion, and global row exhaustion before it
mutates the catalogue.

## Evidence

The focused semantic test is part of the portable ElisaScript gate. To run the
workload and its timing harness directly from the engine root:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 \
/opt/homebrew/opt/python@3.14/bin/python3.14 \
scripts/world_storage_benchmark.py --runs 9
```

On macOS 27.0 / Apple M5, this completed nine fresh-process runs over 64
rounds each with the following result:

```text
world-storage benchmark: runs=9 median_ms=23.784 p95_ms=80.297 min_ms=23.284 max_ms=80.297 binary_bytes=51592
```

Each round registers two schemas, inserts all 256 bounded dynamic rows,
destroys 64 rows from the middle of the dense storage, verifies compaction,
and inserts 64 schema-checked replacements. The measurement covers the
current bounded scalar representation; richer typed payloads and large-world
before/after comparisons remain open in W01 and W10.

## Boundary

Dynamic rows are intentionally limited to eight scalar fields and sixteen
registered types. Typed engine columns remain allocation-free. Native handles,
variable-sized payloads, and persistence are owned by later world and asset
tasks rather than this catalogue boundary.
