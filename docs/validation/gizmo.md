# Manipulation gizmo math (M04)

`Gizmo` (src/animation/gizmo.elisa) is the headless part of the rotate and
translate gizmos. Rays come from `SkeletonPick::perspective_ray` or
`orthographic_ray`.

- `world_basis` / `local_basis(q)`: gizmo axes in world space or along a
  joint's world rotation (the rotation-matrix columns).
- `perspective_size` / `orthographic_size`: world size of a pixel length, so
  handles keep a constant on-screen size.
- `translate(mode, basis, origin, start, now)`: world translation for a drag.
  Axis modes use the closest point on the axis line. Plane modes hit the gizmo
  plane, and `FREE` hits the view plane through the origin. A ray along the
  axis (within 1e-9), a plane edge-on or behind the ray, and an invalid mode
  give zero.
- `rotate(mode, basis, origin, start, now)`: signed ring angle about the
  mode's normal axis. It is NaN for `FREE`, for a missed ring plane, and for a
  drag through the gizmo center.
- `begin` / `cancel`: the drag keeps a copy of the pose, and a cancel returns
  it bit for bit.

## Proofs

`GizmoIndex` (proof/gizmo_index.elisa, 106/106, all replayed).
The source ensures prove:

- a constrained component is always one of the three axes, and an invalid
  mode keeps nothing;
- a plane keeps its two in-plane axes, and `FREE` keeps all three;
- `normal` gives the axis for an axis mode, stays in -1..2 and is -1 for
  `FREE`;
- saved slots are the identity inside the saved range and -1 outside it.

Three rules are covered by tests only: an axis keeps only itself, a plane
drops its normal, and a plane's normal is `mode - 3`. The first two did not
prove (the prover cannot use the equality cases), and the third proved but
left a certificate-replay gap.

## Tests

test/animation_gizmo.elisa checks the results against a Python reference
(1e-9). Local axes are cross-checked with Rodrigues' formula. The test covers
axis, plane and view-plane drags, the quarter turn about Z in both directions,
a local ring, every no-move and NaN case, and an exact cancel. A mutation pass
killed every mutant except the step that removes rounding along the plane
normal, which only affects rounding.
