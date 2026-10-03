# Viewport skinned mesh (plan M03 mesh pass)

## API

- `GlbSkinMesh` (src/assets/glb_skin_mesh.elisa): `load(path)` or
  `from_bytes(bytes)` reads the first skinned mesh node of a GLB. It reads
  every triangle primitive's POSITION, JOINTS_0 and WEIGHTS_0, the indices,
  and the skin's joint nodes and inverse bind matrices.
  - Supported: strided views; u8/u16 joints; f32 or normalized u8/u16
    weights; u8/u16/u32 indices; non-indexed triangle lists.
  - Refused with a `SkinMeshError`, leaving no partial mesh: sparse
    accessors, external buffers, out-of-range accessors, indices or joints.
- `MeshOverlay` (src/viewport/mesh_overlay.elisa):
  - `skin(mesh, joint_models, out)` does linear blend skinning. The joint
    matrix is the model matrix times the inverse bind matrix, and weights
    are renormalised.
  - `draw(list, mesh, skinned, light, style)` appends flat-shaded,
    two-sided triangles to a `ViewportDraw` list. The DEPTH layer is the
    default.
- `SkinMeshPolicy` (src/assets/skin_mesh_policy.elisa) holds the integer
  arithmetic: component sizes, strided spans, corner index range, light
  level over ambient, and channel shading. It is proved, and so are its laws
  in proof/skin_mesh_policy.elisa.

## Validation

`test/viewport_mesh.elisa` (in scripts/check.elisascript):

- A synthetic two-joint mesh:
  - the bind pose is exact;
  - translating and rotating a joint moves its fully and half-weighted
    vertices;
  - a joint count mismatch is refused;
  - a degenerate face is skipped.
- The boxing game's black-boxer.glb, when present:
  - 80964 vertices, 90240 triangles, 52 joints;
  - skinning it with the rest pose reproduces the bind positions (0 µm
    drift);
  - the headless raster at 480x480 inks 24602 pixels
    (build/viewport_mesh.png).
