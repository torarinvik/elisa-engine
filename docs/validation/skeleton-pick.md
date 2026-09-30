# Skeleton picking (M03)

`SkeletonPick` (src/animation/skeleton_pick.elisa) turns a viewport point into
a ray and returns the nearest joint or bone hit with its distance.

- `perspective_ray` / `orthographic_ray`: normalized device point to a unit ray
  for an eye (or view-plane center) and a right/up/forward basis.
- `sphere_hit` / `capsule_hit`: distance along the ray, 0 when the origin is
  inside, -1 on a miss. Capsules are the cylinder plus both end spheres.
- `pick`: joints are spheres, bones are capsules from parent to child
  (reported by the child). Nearest wins; on an exact tie a joint beats a bone
  and an earlier element keeps the pick. Parents that are not earlier joints
  draw no bone.

## Proofs

`SkeletonPickIndex` (proof/skeleton_pick_index.elisa, 71/71): coordinate slots
stay inside the joint table or are -1, a bone link needs `0 <= parent < child
< count`, and `replaces` never lets a miss replace a hit and prefers a joint
on ties.

## Tests

test/animation_skeleton_pick.elisa checks rays and hits against a Python
reference (1e-9; the capsule side against a bisection to 1e-6), an orthographic
pick, overlapping joints and bones resolved nearest-first, ties, misses and
broken parent tables. A mutation pass killed every mutant except
`first < best` in `capsule_hit`, which is equivalent: a valid side hit is never
behind the entry of the capsule's end sphere.
