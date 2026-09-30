# Gizmo handle drawing and hover (M04)

`GizmoDisplay` (src/animation/gizmo_display.elisa) turns a `Gizmo` basis,
origin and screen-constant size into line records for the overlay batch, and
finds the handle under the cursor. `GizmoDisplayIndex` holds the integer
rules, and the prover checks them (proof/gizmo_display_index.elisa, 138/138,
all replayed).

## Drawing

- **Translate:** 21 lines. Each axis is a shaft plus a four-line arrowhead
  (base at 0.8 size, half-width 0.06 size). Each plane handle (Gizmo modes
  3–5) is two edges of a square from 0.25 to 0.4 size along its two axes.
- **Rotate:** three rings of 8–256 segments, each in the plane normal to its
  axis (modes 0–2, matching `Gizmo::rotate`).
- **Colours:** x is red, y green and z blue; a plane takes its normal's colour.
  Hovered is yellow. The handle being dragged is white, which wins over hover.
- **Rejected input draws nothing:** an unknown tool; a basis that is not 3×3;
  an origin that is not xyz; a size that is zero, negative, infinite or NaN;
  or a ring segment count outside 8–256.

## Hover

`hover(tool, basis, origin, size, ray, tolerance)` returns the Gizmo mode of
the nearest handle along the ray, or -1:
- **Axes:** hit within `tolerance` of the shaft, clamped to its ends. A ray
  running along the shaft meets the end it points toward.
- **Planes:** hit inside their squares.
- **Rings:** hit within `tolerance` of the circle.
- **Ties:** an exact tie goes to the lower mode.

Handles behind the ray are ignored, and so are bad input, a negative
tolerance and a NaN tolerance.

## Evidence

test/animation_gizmo_display.elisa (codes 1–43) checks:
- exact record coordinates and colours;
- that rings sit at the gizmo radius in their planes and close on themselves;
- every rejection;
- shaft tolerance and tip clamping;
- plane square bounds;
- nearest-first picking (the y tip over the XZ square, and the z tip over the
  origin when looking down z);
- the x-before-y tie;
- a slanted ray reaching the x ring first;
- local axes from a turned joint.

The mutation check killed every mutant except one. Changing the
parallel-plane epsilon to an exact-zero check survived. That mutant is
equivalent in practice: a zero divisor gives an infinite or NaN parameter,
and every caller already rejects those.

Caller-side restatements of exact cases (translate draws 21 lines, dragging
wins over hover, each plane's axes) timed out in the prover. They stay as
the callee's own proved ensures.

## Still open

- Drawing the handles through the native batch on a live skinned instance.
- Wiring hover to the cursor.
- The live IK drag hook.
