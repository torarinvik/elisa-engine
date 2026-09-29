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
