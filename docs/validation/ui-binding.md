# UI callback bindings

Validated on 2026-10-02 with the pinned Stage1 compiler
(`ELISA_ALLOW_STALE_STAGE1=1`). This is I07 progress.

## Design

- `src/ui/binding.elisa` (`UiBinding`) is a table of up to 32 bindings from a
  widget id to a model field. Handles carry the slot's generation.
- `unbind_owner` drops every binding of a removed widget. A stale handle,
  including one whose slot was reused by a new widget, resolves to -1, so it
  cannot fire into the model.
- Binding past capacity is refused.

## Checks

- `test/ui_binding.elisa` (in the gate) exits 0, codes 1–7: resolve, unbind
  by owner, unrelated widgets untouched, slot reuse does not revive old
  handles, garbage handles, the capacity bound.
- Negative control: ignoring the generation in `resolve` makes the test
  exit 5.

## Gaps

- Fields are integer ids, not typed model paths; no widget or settings screen
  uses the table yet. I07 stays open.
