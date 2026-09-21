# Validator accumulator loops

An Elisa accumulator loop, `for ... |valid: bool = true| -> valid:`, is an
expression. It has a value only when it is the last expression of its block or
is bound to a name. Sixteen validators across eleven modules wrote the loop as a
statement and then ended in `true`. The stage1 compiler discarded the loop's
result, so each validator returned `true` whenever its scalar pre-checks passed.
It skipped every per-row check: uniqueness, finiteness, parent ordering and
storage agreement.

A minimal repro, `all_positive_discarded([1, -2, 3])`, returned `true` under the
current compiler.

## Affected validators

| Module | Function | Row checks that were skipped |
| --- | --- | --- |
| `World` | `world_live_column_valid` | live-id liveness and uniqueness |
| `World::Rollback` | `snapshot_valid` (five loops) | registry storage, transforms, and uniqueness; live-id uniqueness; actor, enemy, and static-object registry agreement |
| `RenderSnapshot` | `snapshot_valid` | row identity, visual asset, and render-ID uniqueness |
| `WorldRendering` | `bindings_valid` | binding identity, transform, and render-ID uniqueness |
| `Pose` | `pose_valid` | finite joint transforms |
| `UiLayout` | `layout_valid` | parent-before-child ordering, non-negative sizes |
| `Ui` | `menu_valid` | positive, unique actions; an enabled focused item |
| `UiText` | `run_valid`, `wrapped_valid` | non-negative widths, increasing line starts |
| `UiWidgets` | `surface_valid` | per-widget validity, unique ids |
| `AssetBrowser` | `browser_valid` | valid, unique asset rows |
| `InspectorView` | `model_valid` | valid fields, unique targets |

`Rollback::restore` relied on `snapshot_valid` as its only defense before it
overwrote `World`. A snapshot with a mismatched registry row was accepted and
copied into the live world.

## Fix

- Each single-loop validator now ends with its loop as the final expression.
- `World::Rollback::snapshot_valid` binds its first four column loops to named
  booleans (`registry_rows_valid`, `live_ids_valid`, `actors_valid`,
  `enemies_valid`). Each is followed by `return false if not ...`. The
  static-object loop is the final expression.
- `RenderSnapshot::snapshot_bind` now rejects a render ID that another gameplay
  identity already owns, matching `WorldRendering::bind`'s `DuplicateRenderId`.
  Previously the public API could build the invalid snapshot that
  `snapshot_valid` exists to reject.
- `scripts/check_module_hygiene.py` rejects an accumulator loop in statement
  position that has a sibling statement after it, across `src/`, `examples/`,
  and `test/`. On the pre-fix tree it reports exactly the 16 loops above. On the
  fixed tree it reports none.

## Regression tests

Each case builds a state that passes the validator's scalar pre-checks and
breaks one row. All cases pass on the fixed sources. Run against the pre-fix
sources, each test stops at its first new case with the listed exit code.

| Test | Case | Pre-fix exit |
| --- | --- | --- |
| `test/world.elisa` | restore rejects a registry row with the wrong storage, a duplicate live id, a negative-health actor, a negative-threat enemy, and an orphan static object; the unmodified snapshot still restores | 56 |
| `test/render_snapshot.elisa` | a second entity cannot claim a bound render ID; the snapshot stays valid | 11 |
| `test/editor.elisa` | forward layout parent, negative word width, unordered line starts, duplicate inspector target, duplicate browser row | 52 |
| `test/inspector.elisa` | duplicate widget id | 28 |
| `test/maze_game.elisa` | duplicate menu action | 89 |
| `test/anim_state.elisa` | NaN joint position | 75 |

## Validation on 2026-09-21

- Passed on the fixed sources: the focused `world`, `render_snapshot`
  (including `WorldRenderingValidation`), `editor`, `editor_session`,
  `inspector`, `anim_state` and `maze_game` executables.
- `scripts/check_module_hygiene.py` passed.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17/17 and 6/6).
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal, including the
  snapshot presenter and the maze world.

The compiler should reject a discarded non-void loop value. Until it does, the
hygiene lint is the guard.
