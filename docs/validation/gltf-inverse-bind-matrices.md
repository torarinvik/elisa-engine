# glTF inverse-bind matrices

**Validated:** 2026-09-23 on macOS 27, SDL3 3.4.16, and Wicked Engine 0.72.114.

The glTF cooker previously checked an `inverseBindMatrices` accessor and then
discarded it. A rig with non-identity bind transforms therefore reached Wicked
with a palette derived only from the rest hierarchy. Cooked skinned meshes now
preserve and use the authored matrices.

## Data path

1. `scripts/cook_gltf_skin.py` reads one float32 MAT4 per skin joint through
   `cook_assets.accessor_bytes`, which honors accessor offsets and strided
   buffer views. It rejects non-finite, projective, and singular matrices.
2. `scripts/cook_gltf_geometry.py` writes the original float stream as
   `skin_inverse_bind_stride=64` and
   `skin_inverse_bind_matrices_b64`, in the source skin's palette order. The
   fields are optional so existing packages and FBX output remain compatible.
3. `native/cooked_geometry_package.h` requires the fields together, checks the
   64-byte stride and exact matrix count, validates every matrix, and rejects
   bind data without a matching skin rig. Snapshot memory accounting includes
   the new stream.
4. `native/render_cooked_mesh.h` converts each authored glTF matrix from
   column-major to Wicked row-vector space and reflects X on both sides before
   storing it in `ArmatureComponent::inverseBindMatrices`. Packages without an
   authored stream keep the existing rest-pose-derived fallback.
5. `native/animation_submission_bridge.h` applies Elisa pose matrices with the
   same X reflection and resets each joint's local transform before applying
   the new pose. This keeps submissions absolute and prevents repeated updates
   from accumulating on the prior transform.

## Evidence

The generated two-joint fixture has translated root and tip bind transforms,
and non-identity inverse-bind matrices. Cooker tests compare the serialized
float stream exactly and reject singular and projective matrices. The native
loader test accepts those exact values and rejects incomplete fields, wrong
stride, short data, NaN, singular matrices, and rig-less bind data; the ASan/UBSan
loader suite passed 94 cases with no failures. The live native probe reads the tip's Wicked
inverse-bind palette entries after `RenderScene::create_mesh`; the coordinate
probe separately checks inverse-bind and submitted-pose matrix reflections.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh \
/opt/homebrew/bin/python3 scripts/render_scene_native_smoke.py
```

This passed, including the SDL3/Metal imported-scene run and packaged maze
sandbox run. The stale-product override was needed because the sibling compiler
checkout's current uncommitted source fails its seed build in
`src/driver/elisac.elisa` with `Ast.File` mutability/region errors. The override
was used only for this engine smoke; no compiler source or product was changed
as part of this work.

One skin per glTF scene and identity transforms on skinned mesh nodes remain
the current cooker limits. Full per-placement snapshot registration also
remains open under A05.
