# Render-scene handedness

The Wicked render service drew Elisa's right-handed, +Y-up world as a mirror
image. Under a right-handed camera, world +X showed on the frame's left. It now
crosses into Wicked's left-handed frame once, through
`native/coordinate_conventions.h`, like the coordinate probe already did.

## The defect

`native/coordinate_conventions.h` defines the boundary. Elisa is right-handed
with +Y up, Wicked is left-handed, and `to_wicked(x, y, z) = (-x, y, z)` negates
X exactly once. The render service didn't use it:
- `apply_camera_look_at` passed the eye, target and up straight to
  `XMMatrixLookAtLH`.
- `set_transform` and `apply_transform` stored positions and rotations
  unconverted.
- `configure_cooked_mesh` and the snapshot mesh path copied vertex positions
  unchanged, then swapped each triangle's second and third index so the faces
  still pointed outward.

Every object and the camera therefore lived in one unreflected frame, and the
left-handed projection reflected the whole image. Nothing looked broken on its
own: faces pointed the right way because of the swap, and only the sides were
wrong. Group 228 of the render smoke caught it. It puts a red strip at world
x = -8 and a blue strip at x = +8, and looks at the origin from (0, 10, 8) with
+Y up. The frame showed blue on the left and red on the right, and the test
was written to expect that.

The Godot backend passes Elisa's coordinates and indices to Godot unchanged.
Godot is right-handed with +Y up too, so it shows the same scene unmirrored.
The two hosts disagreed on which side of the screen +X lands on, which made
this a cross-host parity bug rather than a Wicked-only convention.

## The fix

The render service now reflects every world-space input at the boundary. It
keeps Elisa-space values in its own state and converts only what it writes
into Wicked:

| Input | Conversion |
| --- | --- |
| Instance, snapshot-row and joint rest transforms (`set_transform`) | position `to_wicked`, rotation `to_wicked_rotation`, scale unchanged |
| Camera look-at (`apply_camera_look_at`) | eye and target `to_wicked`, up `to_wicked_direction` |
| Cooked and snapshot vertex positions and normals (`cooked_vector`) | `to_wicked` |
| Cooked tangents (`cooked_tangent`) | xyz `to_wicked`, W unchanged |
| Cooked indices (`assign_cooked_indices`) | authored order |
| Animation poses (`store_joint_pose`) | as transforms |
| Sun direction (`set_environment`) | `to_wicked_direction`, before the sun rotation and weather direction are derived |
| Electric-arc trail and branch points | `to_wicked` per point |

Two helpers joined `coordinate_conventions.h`:
- `to_wicked_rotation(x, y, z, w) = (x, -y, -z, w)`. Conjugating a rotation by
  the X reflection keeps its angle and maps its axis (x, y, z) to
  (x, -y, -z). This matches `elisa_transform_to_wicked`, which the coordinate
  ABI already used for TRS payloads.
- `to_wicked_tangent(x, y, z, w) = (-x, y, z, w)`. Cooked tangents follow
  glTF: `native/mesh_tangent_frames.h` signs W so that the bitangent is
  `cross(N, T) * W`. Wicked's shaders build `cross(T, N) * W`. For the
  reflection R, `R cross(N, T) = cross(RT, RN)`, so the reflected bitangent is
  Wicked's formula with the same W. The coordinate ABI's tangent parity flips
  W at the reflection. That rule is for a bitangent built the same way on both
  sides, so it doesn't apply to cooked meshes.

The reflection reverses every triangle, so authored counter-clockwise front
faces arrive in the order Wicked rasterizes as front-facing. The unconditional
index swap is gone. `assign_cooked_indices` keeps authored order when
`coordinates::winding_reversed` reports that a unit-scale placement reverses
winding, which it always does, and swaps otherwise. Instance scales are
validated positive, so no instance reflects a mesh a second time.

Wicked's own cube, sphere and plane are symmetric under the reflection. They
need no conversion, and their placement goes through `set_transform` like
everything else.

Screen-space overlays, text and panels don't touch world coordinates and are
unchanged.

## Evidence

**Group 228**, `test/render_scene_node_hierarchy_native.elisa`, now expects the
unmirrored sides:
- Case 13: red on the frame's left, green in the center, blue on the right.
- Case 16: after a half turn about +Y, blue on the left and red on the right.
- Case 17 is new. It stages the row at a generic pose, translation
  (1.5, 4, -0.5), rotation (0.2, 0.5, 0.3, 0.8) normalized, scale (12, 1, 8),
  and commits it. The test-only hook
  `elisa_render_scene_v1_test_snapshot_world_matches` builds the Elisa world
  matrix from that pose, reflects it with `elisa_matrix_to_wicked` (S·M·S), and
  compares all 16 entries with the row's Wicked `world` matrix within 1e-4. A
  half turn about +Y is its own reflection, so case 16 alone can't tell a
  converted rotation from an unconverted one. Case 17 can.
- Case 18 retires the row. Cleanup is still 21–23 and 30.

The strips' materials are single-sided, so a wrong winding culls a strip and
its color goes missing.

**Group 230**, `test/render_scene_cooked_texture_native.elisa`, reads its six
columns from the left now: the cutout's opaque green half at 70, the red
backdrop through its clear half at 230, the painted strip's red half at 410
(which the wait loop watches) and blue half at 590, and the glow strip's red
half at 760 and blue half at 900. Each column is the old one mirrored,
1000 − x.

**Group 198**, `test/render_scene_camera_native.elisa`, gains two cases. The
smoke camera sits at x = 0 and looks along x = 0, so its reflection is itself,
and no frame test could see an unreflected camera. Case 130 points the camera
from (3, 10, 8) at (1, 0, -2) with up (0.3, 1, 0.2). The new hook
`elisa_render_scene_v1_test_camera_view_matches` reads Wicked's eye, view
direction and screen right (`GetRight()`, which is Up × At) back into Elisa
space. It checks them against the eye, the normalized direction to the target
and `f × up`, the screen right of a right-handed camera. Case 131 restores the
smoke camera through the same check, which also confirms +X on its right.

**Arc depth** gains exit 158. Its arc runs from (-1, 0, 0) to (1, 0, 0). A
trail's end points carry no offset or lift, and the new hook
`elisa_render_scene_v1_test_arc_endpoints_match` reads the halo's and core's
first and last points back through `from_wicked`.

**The environment probe**, `native/render_scene_environment_probe.h`, expects
Wicked's sun, sun transform and weather direction to be the reflected Elisa
direction.

**The snapshot position probe**, `elisa_render_scene_v1_test_snapshot_position_x`,
reads the row's translation back through `from_wicked`, so the Elisa-side
assertions are unchanged.

The other frame-reading tests, the cooked-material and material-subset groups,
compare the center with both edges and don't depend on sides.

## Mutation checks

Each change below was applied to a copy of `native/`. The smoke's C++ host was
rebuilt against the smoke's Elisa archive and run with the smoke's fixtures.
An unmutated control exited 0.

| Change | Exit | Logged case |
| --- | --- | --- |
| cooked indices swapped again, as before the fix | 228 | 13: the strips are culled |
| cooked positions and normals not reflected | 228 | 13: the strips are culled |
| rotations not converted | 228 | 17: the posed row's world matrix |
| translations not reflected | 227 | 92: the snapshot position check |
| camera eye, target and up not reflected | 198 | 130 |
| only the camera's up not reflected | 198 | 130 |
| sun direction not reflected | 197 | 42: the environment probe |
| arc points not reflected | 158 | none; arc depth exits its own code |
| tangent W flipped | 0 | survived |
| animation poses not reflected | 0 | survived |

Two mutants survived:
- Tangent W only changes the bitangent that normal maps use, and no frame
  check reads it. The sign comes from the math above: the cooked generator
  and Wicked's shaders build the bitangent in opposite orders.
- No render smoke test plays an animation, so the pose path has no native
  check. It uses the same conversions as `set_transform`, which cases 17 and
  92 cover.

The first harness run left `textured_rewrite.elpk` rewritten from the
previous run, so the control failed at 230, case 72. The smoke restores that
copy before every run, and the harness now does too. Mutants that failed
before group 230 were valid in the first run, and the control and the two
survivors were rerun.

## Limits

- **Godot winding.** Godot treats clockwise triangles as front faces, and its
  host uploaded authored counter-clockwise indices unchanged, so its cooked
  meshes were drawn back-facing. This slice didn't change the Godot host.
  (Since fixed, along with the maze tile's inward faces: see
  [`godot-cooked-winding.md`](godot-cooked-winding.md).)
- **Uncovered paths.** Tangent W and animation poses have no native check;
  see the two survivors above.
- **Probe harness.** `native/render_snapshot_bridge.h` is a `wicked_probe`
  fixture that checks row bookkeeping with hard-coded positions. It still
  stores them unconverted, and nothing compares them with Elisa-space
  expectations.

## Validation on 2026-09-21

These ran in two detached worktrees on 58becfc that held only this change,
because other sessions shared the main working tree. `main` later moved to
46117b5, a merge whose tree is identical to 58becfc.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal twice: once
  before the camera and arc cases were added, and once after. That covers:
  - groups 193–199 and 227–230, and arc depth
  - the loader's 74 cases
  - the maze application smoke and the packaged maze cases
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including the Godot probe and both
  Elisa Proof suites (17 and 6 obligations, none failed).
- The mutations above behaved as listed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
