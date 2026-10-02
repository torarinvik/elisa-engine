# UI tree model

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I07 progress.

## Design

- `src/ui/tree_model.elisa` (`UiTreeModel`) is a bounded tree of up to 64
  nodes with stable ids. A node is added under an existing parent (or as a
  root), so parent indices always point earlier and there are no cycles.
- A node is visible when every ancestor is open. Hidden nodes refuse focus.
- `set_open` collapses or expands a node; if focus ends up hidden it moves to
  the nearest visible ancestor in the same call.

## Checks

- `test/ui_tree_model.elisa` (in the gate) exits 0, codes 1–8: refused
  unknown parents and duplicates, subtree hiding, focus repair on collapse,
  hidden focus refused, re-expansion, a middle-node collapse, an unrelated
  collapse.
- Negative control: removing the focus repair makes the test exit 4.

## Gaps

- No removal or reordering, no drawing, no inspector built on it yet. I07
  stays open.
