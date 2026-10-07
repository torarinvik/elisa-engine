# FBX surfaces with eight skin influences — 2026-10-07

Engine main integrates reusable converter, GLB reader and CPU overlay changes
from mocap-track `1cbed086` and normalization repair `230266b8`.
Client-specific import staging and cache/cleanup changes stay in that checkout.

The converter carries the largest skinned triangle mesh, up to 256 joints and
eight influences per vertex, with inverse binds and baked animation. It refuses
unsupported skin data instead of truncating weights. Publishing uses a complete,
synchronized temporary file and atomic no-overwrite link; existing destinations
remain intact. The source FBX is read only.

The GLB reader accepts paired JOINTS_0/WEIGHTS_0 and optional paired
JOINTS_1/WEIGHTS_1, rejects additional sets, validates finite nonnegative weights
and near-unit sums, then normalizes all eight slots. Primitive offsets include
previous vertices. Explicit individual index bounds also prevent overflow in the
policy's sum precondition; this makes its executable summary provable.

Evidence on installed compiler `63585c5f` and prover generation
`f37be51f77cc4de78d6deccdc2bc5134`:

- `scripts/glb_skin_influence_smoke.py` passes O0 and O2: two primitives with
  sums 0.99/1.01, eight nonzero slots, and unpaired/third-set refusal. Restoring
  the old offset fails this control. The shared check now runs this smoke.
- `proof/skin_mesh_policy.elisa`: 180 obligations, no findings or replay gaps.
- Native converter compiles with C99/O2 and Wall/Wextra/Werror.
- `viewport_mesh` passes on Metal: 80,964 vertices, 90,240 drawn triangles,
  zero measured rest-pose drift, and 24,602 inked pixels.
- Independent Blender full-source-weight comparison for Bladed cross evaluates
  36,000 corners at four poses. Maximum world-space nearest-vertex error is
  1.265 micrometres; RMS is 0.317–0.422 micrometres. This comparison measures
  geometry correspondence, not a complete topology or material equivalence.
  The original FBX hash remains unchanged.

Retained local evidence: `build/validation/candidate8-skin-correspondence.json`,
`skin-mesh-eight-proof.json`, `viewport-eight-influence-build.log` and
`skin-influence-offset-mutant.log`.

The complete shared/native gates remain open. Actual Studio asynchronous import
admission is still under client investigation; surface/skeleton toggling and
editing/playback in the packaged app are not qualified by these engine probes.
