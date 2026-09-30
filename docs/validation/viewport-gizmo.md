# Viewport manipulation gizmos (plan M04)

## API

`ViewportGizmo` (src/viewport/gizmo.elisa), with its policy in `GizmoPolicy`
(src/viewport/gizmo_policy.elisa).

- `ViewportGizmo::create(mode, space, model)`: `mode` is `GizmoPolicy::TRANSLATE`
  or `ROTATE`, and `space` is `ViewportGizmo::WORLD` or `LOCAL`. `model` is the
  bone's model transform. `size_px` (default 90) is the arm length or ring
  radius, in pixels.
- `axis(g, i)` gives the gizmo's axes, and `length(cam, g)` gives the arm
  length in world units at the gizmo's depth. The length is screen-constant
  in perspective and orthographic views.
- `draw(list, cam, g, hovered, active)` appends the gizmo to the TOP layer:
  - translate: 3 arrows and 3 plane squares;
  - rotate: 3 rings of 48 segments.
- `pick(cam, g, px, py)` returns the handle under the pointer:
  - `AXIS_X`, `AXIS_Y`, `AXIS_Z`, `PLANE_XY`, `PLANE_XZ`, `PLANE_YZ`, or
    `NONE`;
  - the nearest handle wins, and exact ties go to the lower handle, so axes
    beat planes;
  - the pick tolerance is 7 px.
- Drags:
  - `idle()`, then `begin(&drag, cam, g, handle, px, py, held, rig, bone,
    model)`. This can raise `GizmoError` for these cases:
    - `BadHandle`: a plane handle on a rotate gizmo, or an unknown handle;
    - `NotFinite`: a non-finite pointer;
    - `ShapeMismatch`: the bone is outside the rig;
    - `NoHit`: the constraint is edge-on to the view.
  - `goal(drag, cam, px, py)` returns the constrained goal model transform.
    When the pointer is non-finite or misses, it returns the start transform.
  - `update(&drag, cam, px, py, rig, &held, hook)` calls
    `hook(held, rig, bone, goal)`. The hook writes overrides.
    `gizmo_move_bone` is the default FK hook: it sets the bone's local
    override so the bone's model transform equals the goal, and it keeps the
    local scale. An IK solver passed here moves a whole chain live.
  - `commit(&drag)` keeps the pose. `cancel(&drag, &held)` restores the
    pre-drag overrides, both values and active flags, bit for bit.

The hook type is `fn(mutable PoseOverride::Overrides&, PoseOverride::Rig&,
usize, Geometry::Transform) -> void`. It is spelled through the top-level
aliases `GizmoHookOverrides`, `GizmoHookRig` and `GizmoHookTransform`,
because the stage1 backend declines module-qualified types inside `fn(...)`.
Function values must be top-level functions, not `Module::name`, for the
same reason.

Drags run on a seeded pose: use `PoseOverride::seed_from_clip`, or rest plus
overrides. Then `evaluate_rest` is exactly the displayed pose.

## Proof

`GizmoPolicy` covers these facts:

- Handle classification (`is_axis`, `is_plane`), and `allowed(mode, handle)`
  (rotate takes axis handles only).
- `normal_axis` is always in range: 0..2 for real handles, 3 (`NO_AXIS`)
  otherwise.
- The drag state machine:
  - `next_state` only returns IDLE or DRAGGING;
  - BEGIN always ends in DRAGGING;
  - COMMIT and CANCEL always end in IDLE;
  - MOVE keeps DRAGGING;
  - any two-event sequence stays inside the two states.
- The pick order (`better`): nearer first, and the lower handle on a tie.

```
/private/tmp/claude-501/mc/elisa-proof "$PWD/proof/gizmo_policy.elisa"
# verification state: proved, 113/113, certificate replay: 113 replayed, 0 gaps
```

## Test

```
ELISA_ALLOW_STALE_STAGE1=1 bash ~/.elisac/stage1/scripts/elisac_stage1.sh -O0 -o build/viewport_gizmo.o test/viewport_gizmo.elisa
clang -o build/viewport_gizmo-test build/viewport_gizmo.o build/elisa_native_fallbacks.o ~/.elisac/stage1/build/runtime/elisacore_runtime.o
./build/viewport_gizmo-test   # rc 0
```

The test is also in the `scripts/check.elisascript` list. It covers:

- **Size.** The arm is 90 ± 0.5 px in a front orthographic view at two
  zooms. In perspective, its screen-parallel part is 90 ± 1 px at two zooms.
- **Axes.** LOCAL X of a bone turned 90° about Y is −Z; WORLD X stays X.
- **Draw.** Translate draws 6 handles and rotate draws 3.
- **Pick.**
  - Arrows pick their axis, 4 px off the line still picks, and the square
    picks its plane.
  - At the centre, the camera-facing Z arm is nearest and wins.
  - Exact ties go to the axis before the plane.
  - Empty space, or a NaN pixel, picks NONE.
  - A rotate ring picks on its rim, not inside it.
- **Constrained translation.**
  - A drag along X, 50 px right and 40 px up, moves exactly 50 px of X.
  - An XY-plane drag follows both components.
- **Rotation.** A quarter turn of the pointer about Z turns the bone's X
  axis onto Y, and the position is unchanged. The default FK hook applies
  this to the bone's model transform.
- **IK hook.** A two-bone leg IK written in the test as the app's own solver
  is used. On each of 5 updates:
  - the hook runs exactly once;
  - the foot's model position lands on the goal within 1 mm;
  - the knee has moved.
- **Cancel.**
  - After IK drags, every override's value and active flag are bit-identical
    to the pre-drag state, and the foot's model transform is bit-identical
    to the one before the drag.
  - A bone that was overridden before the drag keeps its seeded value.
- **Adversarial.**
  - These `begin` calls raise and leave the drag idle:
    - a plane handle on a rotate gizmo, NONE, or handle 7 raises
      `BadHandle`;
    - a NaN pointer raises `NotFinite`;
    - bone 3 in a 3-bone rig raises `ShapeMismatch`;
    - an edge-on Z axis, YZ plane or X ring raises `NoHit`.
  - When idle, `update` calls no hook and `cancel` returns false.
  - A NaN pointer mid-drag keeps the start transform.
  - A double commit is harmless.

## Open

- Snapping, and a free view-plane translate handle.
- Gizmos over the real skinned mesh (see M01).
- Rotating about the bone's parent pivot. The gizmo rotates the bone in
  place, which is what FK on the bone itself means.
