# Skeleton display records

`SkeletonDisplay` (src/animation/skeleton_display.elisa) turns one pose into
line records for the M03 skeleton display.

- **Bones:** each bone is a line from a joint to its parent. Roots, self-links
  and parents outside the skeleton draw nothing.
- **Joints:** each joint can have a three-axis star of half-size `joint_size`,
  built by `OverlayLines::points`.
- **Colours:** normal is pale blue, hovered is yellow and selected is orange.
  A bone takes its child joint's highlight, and selection wins over hover.
- **Batching:** the records use the overlay line format, so they go to Wicked
  through `RenderScene::debug_line_records` with the other overlays.
- **X-ray:** `SkeletonDisplayIndex::depth_tested(xray)` turns depth testing
  off, so bones draw through the mesh.
- **Rejected input:** the result is empty for more than 256 joints, a pose that
  is not three values per joint, or a joint size that is negative, NaN or at
  least 1e30.

`SkeletonDisplayIndex` (src/animation/skeleton_display_index.elisa) holds:
- the highlight rule;
- the record count, one per bone plus three per joint when joints are shown;
- the x-ray rule.

Their `ensure` clauses state the exact rules, and the proof proves them all:
- selected when the joint is selected;
- hovered only when it is not selected;
- normal for every other joint, and for any joint outside the skeleton;
- the record count, which is at most 1,024;
- x-ray off means depth-tested.

The proof (proof/skeleton_display_index.elisa) is 52/52, with every obligation
replayed. Four caller-side restatements of the exact rules timed out, so the
proof keeps only the range, outside-joint, bound and x-ray wrappers.

## Validation

test/animation_skeleton_display.elisa runs in the check gate on a
four-joint chain with a branch.

- **Bone colours:** each bone's colour and ends are checked for the selected,
  hovered and normal cases.
- **Record counts:** they match `display_lines` with and without joint stars.
- **Stars:** each joint's star sits at the joint in its role's colour.
- **Highlight edge cases:** selection wins over hover, and selection or hover
  outside the skeleton lights nothing.
- **Rejected input:** ragged or over-long poses; negative, NaN or huge sizes;
  257 joints (256 draw 255 bones); self-links and out-of-range parents.
- **Z coordinate:** bone ends keep their own z.

The mutation check caught every mutant except one equivalent mutant. Turning
the zero-size early return into a negative-size one still draws no stars,
because `points` rejects a zero size too.

## Still open

This covers only the records. Still open for M03:
- drawing the records over a skinned mesh in the SDL3/Metal smoke test;
- the mesh-visibility toggle (`set_visible` already exists) and the wireframe
  toggle (Wicked's `SetWireframeMode`);
- picking in every M01 viewport.
