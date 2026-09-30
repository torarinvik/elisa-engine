# Skeleton display and picking (M03)

## API

The overlay is `src/viewport/skeleton_overlay.elisa`, module `SkeletonOverlay`.
It includes the viewport draw list and `PoseOverride`.

Drawing and style:

- `draw(&list, &cam, &rig, &models, &style, selected, hovered) -> usize error[OverlayError]`
  - Bones are drawn as lines from the parent joint to the child joint.
  - Joints are drawn as screen-sized markers.
  - `style.xray` draws into `ViewportDraw::TOP`, over the mesh. Otherwise the
    skeleton draws into `DEPTH`.
  - Colour priority: selected, then hovered, then the base colour.
  - `models` must come from `PoseOverride::evaluate` or `evaluate_rest`. A count
    mismatch raises `ShapeMismatch` and draws nothing.
- `Style`: `joint_px`, `bone_px`, colours, `xray`, `show_mesh`, `wireframe`.
  `default_style()` returns the defaults.
- `mesh_mode(&style)` returns `MESH_HIDDEN`, `MESH_SHADED` or `MESH_WIREFRAME`
  for the host's mesh pass.

Picking:

- `pick(&cam, &rig, &models, &style, px, py) -> Hit{kind, bone, distance}`
  - `kind` is `SkeletonPickPolicy::NONE`, `JOINT` or `BONE`.
  - `distance` is measured along the pick ray, in metres.
  - Joints are spheres and bones are capsules. Their radii are
    `joint_px`/`bone_px` pixels at their own depth, so the tolerance is the same
    at any zoom and in orthographic views.
- `ray_sphere` and `ray_capsule` are exposed. Targets are built with
  `nothing()`, `joint(i)`, `bone(i)` and `target_of(hit)`.

Tie-break: `src/viewport/skeleton_pick_policy.elisa`, module `SkeletonPickPolicy`.

- `better(code, kind, bone, ...)` is the strict lexicographic order:
  1. nearer first, with distance in micrometre codes;
  2. a joint before a bone;
  3. the lower index.
- The pick winner therefore does not depend on the order candidates are visited.

## Evidence

The test is `test/viewport_skeleton.elisa`, listed in `scripts/check.elisascript`.
It passes in four views:

- the default perspective view;
- an orbited 640x360 perspective view;
- `FRONT` orthographic;
- `SIDE` orthographic.

The same checks run in each view:

- A joint 0.4 m in front of the spine joint, on the same line of sight, wins:
  nearest first. The reported distance is where the ray enters its sphere.
- Three joints coincide at the hips, and four bones end there. Joint 0 wins:
  joints beat bones, and the lower index wins.
- The leg joint, the leg bone mid-point and the spine bone mid-point pick the
  expected targets.
- 3 px beside a bone hits it, and 12 px beside it misses.
- Adversarial inputs return NONE:
  - an empty corner;
  - a NaN pixel;
  - a models array shorter than the rig.
  With the short models array, `draw` raises `ShapeMismatch` and leaves the list
  empty.
- `draw` covers all six joints. X-ray output goes only to TOP; with x-ray off,
  the same lines go to DEPTH.

Colour priority, the mesh modes, the ray and sphere and capsule primitives
(including a miss, the origin inside a sphere, and capsule end caps) and the
tie-break order are unit-tested.

Proof: `proof/skeleton_pick_policy.elisa` is **proved, 47/47**. The six ensures
on `better` state each ordering rule in both directions, and together they
define the lexicographic order. Two prover limits were found:

- Ensures written with `!=` were not established; they are written with `<` and
  `>` instead.
- Relational lemmas that call `better` twice from a caller time out, as does
  monotonicity through a constant multiply (`x*1024 < y*1024`). So
  irreflexivity, asymmetry and totality are stated by the callee ensures, not as
  separate two-call theorems.

Build: `ELISA_ALLOW_STALE_STAGE1=1 bash ~/.elisac/stage1/scripts/elisac_stage1.sh -O0 -o build/viewport_skeleton.o test/viewport_skeleton.elisa`,
then link with `build/elisa_native_fallbacks.o` and the stage1 runtime object.

## Open

- The skinned mesh is still not rendered into the viewport ring (see
  viewport-shared-texture.md). X-ray and wireframe are therefore exercised as
  draw-layer and mode decisions, not as pixels over a mesh.
- Hover is a pick on pointer move in the host app; the engine supplies the pick
  and the colours.
