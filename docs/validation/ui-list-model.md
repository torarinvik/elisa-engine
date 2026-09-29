# UI list model

Validated on 2026-09-29 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I07 progress.

## Design

- `src/ui/list_model.elisa` (`UiListModel`) is a bounded list of up to 64 rows
  with stable ids. Focus is stored by id, not by position.
- `remove` and `move` repair focus in the same call: removing the focused row
  moves focus to its successor (predecessor at the end, none when empty);
  reordering keeps focus on the same row.
- `window_first`/`window_count` realise at most one viewport of rows however
  large the collection is; `reveal_focus` scrolls the focused row into view.

## Checks

- `test/ui_list_model.elisa` exits 0 (codes 1–17): capacity and duplicate
  refusal, bounded window, focus by id, reorder, invalid moves, focus repair
  on removal, unfocused removal, draining.
- Negative control: letting focus survive removal of its row (dropping the
  `not keep` condition) makes the test exit 15.

## Gaps

- No typed model binding, tree view, slider, inspector, validation state or
  callback lifecycle; no widget draws from this model yet. I07 stays open.
- No proof harness for this module.
