# Skeleton picking (M03)

`SkeletonPick` (src/animation/skeleton_pick.elisa) turns a viewport point into
a ray and returns the nearest joint or bone hit with its distance.

- `perspective_ray` / `orthographic_ray`: normalized device point to a unit ray
  for an eye (or view-plane center) and a right/up/forward basis.
- `sphere_hit` / `capsule_hit`: distance along the ray, 0 when the origin is
  inside, -1 on a miss. Capsules are the cylinder plus both end spheres.
- `pick`: joints are spheres, bones are capsules from parent to child
  (reported by the child). Nearest wins; on an exact tie a joint beats a bone
  and an earlier element keeps the pick. A joint's own index or an out-of-range
  parent draws no bone.

## Proofs

`SkeletonPickIndex` (proof/skeleton_pick_index.elisa, 71/71): coordinate slots
stay inside the joint table or are -1, a bone link needs the child and parent in
range and distinct, and `replaces` never lets a miss replace a hit and prefers a joint
on ties.

## Tests

test/animation_skeleton_pick.elisa checks rays and hits against a Python
reference (1e-9; the capsule side against a bisection to 1e-6), an orthographic
pick, overlapping joints and bones resolved nearest-first, ties, misses and
broken parent tables. A mutation pass killed every mutant except
`first < best` in `capsule_hit`, which is equivalent: a valid side hit is never
behind the entry of the capsule's end sphere.

## Live viewports

`scripts/viewport_pick_smoke.py` builds `test/render_scene_viewport_pick_main.elisa`
on SDL3/Metal. It sets up three viewports over one scene:

- a perspective primary viewport on the left;
- an owned orthographic front camera (looking -z) at top right;
- an owned orthographic side camera (looking -x) at bottom right.

Every pixel centre of each viewport goes through `RenderScene::camera_ray` or
`RenderScene::camera_viewport_ray` and then `SkeletonPick::pick` on a four-joint
skeleton. The checks are:

- every ray is accepted;
- every joint hit lies on its sphere to within 1e-4;
- perspective rays fan out and orthographic rays are parallel;
- the perspective and front views find all four joints;
- the side view never returns joint 1, because joint 3 sits in front of it, so nearest-first must hide it;
- a ray outside the side viewport is rejected.

The run passed on 2026-09-30. A mutant that made joint picks prefer the farther hit was caught (exit 14). The smoke is run by hand and is not in the gate. These are the engine's own camera viewports; hosting them as M01 shared Metal textures is still open.

## Picking in the M01 shared-texture viewports (2026-10-01)

`ViewportScene::pick` casts `ViewportCamera::ray` for a pixel into `SkeletonPick::pick`. Joints catch within `PICK_PIXELS` (6) at the camera's target depth and bones within half that. `test/viewport_scene_pick.elisa` (build with `scripts/build_viewport_native_test.sh viewport_scene_pick`) renders one scene into a perspective, a front and a side IOSurface viewport and checks each view:
- Every joint picks itself at its projected pixel, and the rendered frame shows the joint marker there (within a 3x3 block; bone lines can cover the exact centre).
- A bone's midpoint picks the bone.
- An empty corner picks nothing.

In the side view two joints overlap exactly, and the nearer one (+x, toward the camera) wins. Picking a joint in the front view and selecting it turns that pixel orange after the redraw. Two mutants are caught with exit 112: a flipped pixel y, and swapped joint and bone radii.
