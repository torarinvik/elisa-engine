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
- `remove` drops a node and its whole subtree and compacts the rest in order,
  remapping parent indices. Focus inside the removed subtree moves to the
  removed node's parent, or to the first remaining root when it had none.

## Checks

- `test/ui_tree_model.elisa` (in the gate) exits 0, codes 1–13: refused
  unknown parents and duplicates, subtree hiding, focus repair on collapse,
  hidden focus refused, re-expansion, a middle-node collapse, an unrelated
  collapse, subtree removal with parent remapping and focus repair, removal
  of roots and unknown ids.
- Negative controls: removing the collapse focus repair makes the test exit
  4; never repairing focus on removal makes it exit 9.

## Gaps

- No reordering, no drawing, no inspector built on it yet. I07
  stays open.

## Sibling reordering (2026-10-02)

`UiTreeModel::move_up(t, id)` moves a node and its whole subtree before its
previous sibling; `move_down` moves it after its next sibling. Rows are rebuilt
by a stable permutation (the moved subtree takes the sort key just below the
sibling it passes), so every parent still precedes its children and `remove`'s
single-pass remap stays valid. Focus is tracked by id and stays on its node.
`test/ui_tree_model.elisa` codes 14-19 check sibling order, parent remapping,
the first-sibling no-op, move_down undoing move_up and removal after
reordering. Negative control: writing the old parent index instead of the
remapped one exits 15.
