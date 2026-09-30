# glTF matrix decomposition (M06, tenth slice)

`GltfMatrixTrs` (`src/assets/gltf_matrix_trs.elisa`) splits a node `matrix`
into translation, rotation and scale. `GltfRestPose` now reads matrix nodes
through it, instead of accepting only the identity.

- `decomposable(m, tolerance)` accepts only an affine 4×4 matrix: the bottom row
  must be 0 0 0 1, no column may be zero, and the columns must be orthogonal
  to within `tolerance` relative to their lengths.
  `GltfRestPose` uses a tolerance of 1e-4, so float32-rounded exports still
  pass.
- `decompose` returns ten values:
  - translation, taken from column 3;
  - scale, the column lengths, with a negative determinant folded into X;
  - rotation, using Shepperd's largest-component method, with w ≥ 0.
- `GltfRestPose::problem` reports `Matrix` (5) in three cases: a malformed
  matrix, a matrix that is not a TRS, or a matrix together with
  translation/rotation/scale members, which glTF forbids.

## Tests

`test/assets_gltf_matrix_trs.elisa` checks:

- 12 Python-built TRS values split back to 1e-9. These include the identity,
  half turns, a near half turn, a mirrored X scale, and random rotations
  with non-uniform scale;
- exact half turns about X, Y and Z;
- shear, a projective row, a zero X column, a flat matrix and a wrong size
  are rejected;
- a float32-rounded matrix is accepted.

`test/assets_gltf_rest_pose.elisa` adds:

- a matrix node split into (5, 6, 7), a 90° turn about Z and scale (2, 3, 4);
- a sheared matrix, a matrix beside `scale`, and a bad bottom row are
  rejected.

## Mutation check

These mutants survive:

- moving the `trace > 0` threshold;
- dropping the `r00 > r11` part of the X-branch condition.

Every Shepperd branch is exact in real arithmetic, so these conditions only
decide precision. Every other mutant fails the tests: the determinant flip,
the sign normalization, the translation column, the branch formulas, the
bottom-row and zero-column checks, the orthogonality pairs, and the
rest-pose wiring.
