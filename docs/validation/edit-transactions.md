# Edit transactions

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is E03 progress.

## Design

- `src/tooling/transactions.elisa` (`EditTransactions`) is a pure edit
  history over a packed scene document (`Doc`, up to 15 nodes, each with an
  alive bit and a 4-bit parent link).
- Operations: create, delete, reparent. Each is recorded with what is needed
  to invert it (`old_parent`) and a `group` id.
- `apply` refuses cycles, dead or duplicate ids, deleting a node that has
  children, and a full history (capacity 24). Recording a new edit drops the
  redo tail.
- `undo` and `redo` step a whole group at once and fail atomically: a group
  that cannot be fully replayed leaves the document and history unchanged.

## Checks

- `test/edit_transactions.elisa` exits 0 (codes 1-15).
- Negative control: removing the cycle check makes it exit 3.
- Wired into `scripts/check.elisascript` with the other portable tests.

## Gaps

- No proof. The prover cannot bound `(parents >> shift) & 15` (nor
  `% 16`), so `link_at` is unproven; this is a prover hole for a later fix.
- No editor UI is connected to the history yet, and it is not persisted.
- Documents are limited to 15 nodes.
