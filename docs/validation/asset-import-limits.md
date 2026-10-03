# Asset import limits

Validated on 2026-09-29 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is A12 progress.

## Design

- `src/assets/import_limits.elisa` (`AssetImportLimits`) holds the resource
  budget an untrusted importer charges as it reads a file: bytes, elements,
  nesting depth and work units.
- `charge_alloc` checks a declared `count * each` against the remaining
  allowance by division, so a hostile count such as 9e18 is refused before any
  multiplication can overflow. Nothing is charged when a request is refused.
- `enter`/`leave` cap and balance nesting; `charge_work` bounds work.
- Refusals are named: `TooLarge`, `TooMany`, `TooDeep`, `OutOfWork`, `Invalid`.

## Checks

- `test/assets_import_limits.elisa` exits 0 (codes 1–13): exact-allowance
  acceptance, hostile counts, depth and work caps, unbalanced `leave`,
  invalid limits.
- Negative control: replacing the division check with `count > l.max_bytes`
  makes the test exit 3.

## Gaps

- No importer calls this yet, and there are no worker processes, timeouts or
  fuzz corpora. A12 stays open.
- No proof harness for this module.

## GLB import under limits (2026-10-02)

`GlbImport` (`src/assets/glb_import.elisa`) is the first importer that uses the budget:

- `parse_limited` charges the image size as a byte allocation, and the chunk walk as two
  work units, before it reads the header.
- `read_bin_limited` refuses a malformed range first and only then charges the copy, so
  a malformed read costs nothing.
- A refusal is `OverBudget` or `Malformed`.

`test/assets_glb_fuzz.elisa` (in the gate) builds a seeded corpus of 512 mutants of a
valid GLB with a BIN chunk. Each mutant is one of: a byte overwrite, a truncation, a
random header or chunk length word, or appended junk.

- **Reproducible.** Two runs of the corpus give the same verdict digest (code 2).
- **Layout invariants.** Every accepted layout lies inside the image, and its whole BIN
  payload reads back (code 3).
- **Outcomes.** 96 mutants parse and 416 are refused as malformed (code 4).
- **Byte budget.** A 64-byte limit refuses the image without charging anything (code 5).
- **Work budget.** A 5-unit limit allows two parses and refuses the third (codes 6–7).
- **BIN reads.** A read one byte past the byte limit is refused (codes 9–10). An
  out-of-range read leaves the budget at zero (code 11).

Negative controls:

| Change | Fails at |
|---|---|
| Drop the size charge | 5 |
| Drop the range precheck | 11 |
| Charge before the range check (first draft) | 11 |
| Skip the JSON nesting check in `tokenize_limited` | 13 |

`GlbImport::tokenize_limited` tokenizes the JSON chunk under the same budget. It charges one
work unit per JSON byte before tokenizing. Nesting deeper than `max_depth` is refused, and so is
a token table larger than the element or byte budget (40 bytes per token); a refusal leaves no
tokens. The fuzz test covers acceptance, depth, work, element and malformed cases (12-16).

`GlbDocumentImport::from_bytes_limited` builds an animation `GlbDocument` only after the container
and JSON pass those checks, then charges the document's byte copy, 64 bytes per node and 4 bytes
per decoded f32. `test/assets_glb_document.elisa` (cases 80-84) checks the exact byte total, depth
and copy refusals and a truncated file; dropping the decoded-value charge fails at 81.

The native cooked-model loader (`load_cooked_geometry_bytes`) has a seeded corpus in
`native/cooked_geometry_fuzz.h`: 2048 mutants of the decoded mesh section (bit flips, extreme 32-bit
fields, truncation, duplicated spans), run twice for a matching digest by
`native/package_format_test.cpp` (exit 17) in `scripts/test_elisa_package.py`. Every mutant is
refused with a message or keeps the stream invariants (positions, normals, UVs, triangle indices in
range): 259 accepted, 1789 refused, 0 broken, also clean under ASan+UBSan. Disabling the index range
check lets 11 broken mutants through and fails with 17.

Still open: model, image and animation importers beyond the container, out-of-process
workers, and timeouts.

`fuzz_binary_package` in the same header mutates whole binary packages (512 seeded mutants, run twice for an identical digest) and reads the index and every section. Sections must stay inside the file, payloads must match their unpacked size, the mesh must hold the geometry invariants, and every refusal must carry an error. It found zstd sections whose frame decoded shorter than the declared size passing the checksum; `virtual_package.h` now refuses them. Result: 288 indexed, 224 refused, 0 broken; clean under ASan+UBSan; without the size check two mutants break and the test exits 17.
