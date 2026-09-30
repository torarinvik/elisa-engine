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

## Live SDL run

`scripts/gizmo_cursor_smoke.py` builds `test/render_scene_gizmo_cursor_main.elisa`
on SDL3/Metal. Pointer events are pushed through the test probe and read back
with `Application::next_pointer_event`. Each one becomes a `GizmoCursorIndex`
event and a `RenderScene::camera_ray` ray.

1. A move over the x shaft hovers, a left press begins an `EffectorDrag`, a move 40 px right drags and a left release commits. The actions must be HOVER, BEGIN, UPDATE, COMMIT, and the tip must move toward +x.
2. A second drag from the committed pose ends with a right press. The actions must be HOVER, BEGIN, UPDATE, REVERT, and the pose must return exactly to the committed one.

Result on 2026-09-30: `moved=7239` pixels from rest to the committed drag, and `cancel_drift=0` between the committed and cancelled captures. A mutant that corrupted the reverted pose was caught (exit 24). The smoke is run by hand and is not in the gate.
