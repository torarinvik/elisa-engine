# Gizmo cursor wiring

`GizmoCursor` (src/animation/gizmo_cursor.elisa) turns pointer events into gizmo interaction.

Converting the pointer position:
- `ndc` converts a pixel to a normalized device point, with x right and y up in [-1, 1].
- `SkeletonPick`'s camera rays turn that point into a ray.

`feed` runs a two-state machine, idle or dragging:

| State | Event | Result |
|---|---|---|
| Idle | move | Updates the hovered handle. |
| Idle | left press on a handle | Starts a drag and keeps the press ray. |
| Dragging | move | Reports the translate delta or the ring angle, measured from the press ray. |
| Dragging | left release | Commits the drag. |
| Dragging | right press | Cancels the drag. |

Hover stays frozen while a drag is in progress. The app applies each report itself, for example the target and cancel of `EffectorDrag`.

`GizmoCursorIndex` holds the state machine, the per-state actions and the mapping from raw pointer events. It is proved in proof/gizmo_cursor_index.elisa (157/157, all replayed). The prover handles single-condition guards and literal codes, so the bodies are written that way, with one helper per state. The dispatcher `action` carries no ensures of its own: restating its helpers' rules timed out.

test/animation_gizmo_cursor.elisa covers:
- pixel mapping, and a centre ray that runs along the camera's forward axis;
- hover over empty space and a press that misses;
- a translate drag along x that ignores z motion;
- frozen hover, and a second press during a drag being ignored;
- commit and cancel;
- a quarter-turn drag on the y ring (-π/2);
- the raw pointer-event mapping.

The tests are mutation-checked.

Still open: feeding live SDL pointer events in an interactive run.
