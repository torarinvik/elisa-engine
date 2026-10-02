# UiInspector

`src/ui/inspector.elisa` adds typed entry and validation error states to a `UiSettingsPage`. The editor's property inspector and the game's settings pages therefore share the same rows, bindings and focus rules.

- `commit(ins, row, field)` parses the `UiEditField` text as ASCII decimal, with an optional `-` and at most 18 digits, so the value can't overflow.
- Each row gets an error state: `NOT_NUMBER`, `OUT_OF_RANGE` or `OFF_GRID` (the row's top value is always allowed). A rejected entry keeps the last good value. The next accepted entry clears the error.
- Removing a row drops its error and stales its binding handle. Commits to a removed row are ignored.

`test/ui_inspector.elisa` (in the gate) covers each error kind, per-row errors, clearing an error, the edit reaching its bound field, and removal. Negative control: dropping the grid check fails with code 4.
