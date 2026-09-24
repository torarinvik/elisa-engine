# Dynamic world-storage validation

`WorldStorage::Catalog` keeps engine-owned actor and projectile columns next to
bounded game-defined records. Game-defined types register a fixed schema of one
to eight `i64`, `f64`, or `bool` fields, share the catalogue's monotonic
identity stream, and compact dense rows after destruction. Public entity
references are opaque `Handle` values branded to their creating catalogue, so
equal local IDs in separate catalogues cannot alias. The brand and ID fields
are private; callers can inspect a handle's local ID for diagnostics, but
cannot construct a handle from an integer. Field values remain in fixed inline
arrays: integer and boolean payloads share the integer lane, while real values
use a separate lane. There is no per-row heap allocation.
Spawn rejects unknown types, kind mismatches, malformed values, per-type
capacity exhaustion, and global row exhaustion before it mutates the
catalogue. Per-type live counts keep capacity checks constant-time and are
checked against the dense rows by `catalog_registered_counts_valid`.

## Evidence

The focused semantic test is part of the portable ElisaScript gate. To run the
workload and its timing harness directly from the engine root:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN=/Users/torarinvikbjarko/.elisac/elisac-stage1 \
/opt/homebrew/opt/python@3.14/bin/python3.14 \
scripts/world_storage_benchmark.py --runs 31
```

On macOS 27.0 / Apple M5, this completed 31 fresh-process runs over 64
rounds each with the following result:

```text
world-storage benchmark: runs=31 median_ms=21.836 p95_ms=56.856 min_ms=13.942 max_ms=62.691 binary_bytes=69400
```

Each round registers two schemas and visits all 256 bounded dynamic rows,
destroying 64 scheduled rows during insertion so the workload can retain only
opaque handles without exposing a handle-array representation. It then checks
the final row count and inserts 64 schema-checked replacements. The nine-run
sample had a 21.836 ms median and a 56.856 ms p95. The spread across fresh
processes shows substantial host scheduling noise, so use these numbers as a
local baseline rather than a precise regression threshold. This interleaved
deletion order is not directly comparable with the earlier fill-then-delete
baseline. The executable is 69,400 bytes. Large-world before/after
measurements remain open in W10.

## Boundary

Dynamic rows are intentionally limited to eight scalar fields and sixteen
registered types. Typed engine columns and registered rows remain
allocation-free. Variable-sized payloads and persistence are owned by later
world and asset tasks rather than this catalogue boundary.
