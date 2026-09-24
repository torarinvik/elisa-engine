# Godot cooked-mesh winding

The Godot host drew every cooked triangle from the side glTF calls its back.
It now reverses each triangle's winding once, at its upload boundary in
`backends/godot/cooked_mesh.gd`. The check that confirmed this also showed that
the maze tile fixture wound half its faces inward. That is fixed too.

## The defect

Cooked packages keep glTF's winding: a front face is counter-clockwise seen
from the side it faces. The Wicked render service keeps that order after
reflecting into Wicked's left-handed frame; see
[`render-scene-handedness.md`](render-scene-handedness.md).

Godot is right-handed with +Y up, like Elisa, so positions and normals cross
unchanged. Winding does not: Godot draws clockwise triangles as front faces,
and its own glTF importer swaps each triangle's second and third index for that
reason. `probe.gd` and `capture.gd` each built their `ArrayMesh` with the cooked
indices unchanged, so every cooked front face reached Godot as a back face.

No check could see it:
- The goal marker draws the maze tile, a closed box, unshaded in one color. A
  closed mesh with its front faces culled shows its far faces' insides in the
  same silhouette. On the face the capture sees, the tile's own winding also
  cancelled the error; see below.
- The headless probe only counted surfaces.

## Confirmation

A scratch script rendered cooked triangle 0 of `build/cooked/maze_tile-godot.pkg`
alone in Godot 4.7.2 with a display. It drew the triangle white on black with
back faces culled, and read the center pixel. The camera looked at the
triangle's center from 3 units along its cooked normal, +Z, which is also its
glTF front, and then from behind.

| Index order | From the front | From behind |
| --- | --- | --- |
| cooked order, the old upload | culled | drawn |
| second and third index swapped | drawn | culled |
| Godot's own glTF import of the same triangle | drawn | culled |

The project's Compatibility renderer (OpenGL 4.1 on Metal) and Forward+
(Metal 4.0) agreed. In all three orders, `Plane(a, b, c)` gave a normal that
points to the side Godot drew. Godot documents that constructor as taking
points in clockwise order.

## The fix

`backends/godot/cooked_mesh.gd` is now the one place cooked geometry enters
Godot:
- `surface_arrays(package)` decodes positions, normals and indices.
- It passes the indices through `godot_indices`, which swaps each triangle's
  second and third index.

The native cooked package uses meshoptimizer's lossless stream codec, which
the GDScript host does not decode. `scripts/cook_assets.py` therefore emits a
`*-godot.pkg` companion from the same normalized geometry, with raw vertex and
index streams. The Godot probe and capture load that explicit host variant;
they do not fall back to Godot's source importer for rendered geometry.

`probe.gd` and `capture.gd` both build their meshes from it. The skinned quad
in `probe.gd` has hand-written indices and isn't cooked, so it doesn't use the
helper.

## The maze tile fixture

`examples/maze/assets/maze_tile.gltf` wound six of its twelve triangles
inward. Triangles 2–5 and 8–9, the −Z, +X and +Y faces, were counter-clockwise
from inside the box, against their own normals. Each pair of opposite faces
shared one winding.

Under the old Godot upload, the two errors cancelled on those faces. The
capture camera sits at z = −5 and sees the goal marker's −Z face, so that face
drew. Once the upload was fixed, the face was culled, the marker became a
hollow outline, and the capture's topology check reported one mismatch. The
Wicked render service already honored glTF winding, so it drew those three
faces inside out as well.

The fixture now swaps the second and third index of those six triangles, and
nothing else changes: 12 bytes of the embedded index buffer. Every triangle is
counter-clockwise from outside and agrees with its normals. The vertex and
index counts are unchanged at 24 and 36. The source hash changed from
`ca3779a6…` to `a135307c…`, and cooking picks up the new source.

With both fixes, the Godot capture differs from the frame before either in 6
pixels, all on the goal marker's edges (x 94–105, y 34–45 of 320×200).

## Checks

**Headless, in `scripts/check.elisascript`.** Culling needs a renderer, which
`--headless` lacks, so `probe.gd` compares front-face normals instead. It
uploads the manifest's cooked package through `cooked_mesh.gd` and imports the
source glTF with Godot's `GLTFDocument`. Two counts must equal the triangle
count:
- `facing_godot_import`: for each uploaded triangle, the imported triangle over
  the same three positions has a `Plane` normal on the same side. This isolates
  the upload boundary, whatever the asset's winding.
- `facing_normals`: each uploaded triangle's `Plane` normal agrees with the sum
  of its vertex normals. This catches an asset wound against its normals, which
  the import comparison can't see.

**Rendered, in `scripts/godot_capture.elisascript`.** `winding_capture.gd`
draws each cooked triangle alone, from its glTF front and from behind, and
requires it to show from the front only. It runs after cooking and before the
extension build, and needs a display driver like the rest of that script. The
existing topology check in the same script caught the fixture through the
goal marker.

## Mutation checks

Each change was applied to a copy of the Godot project. The copy used the
cooked package that matches its fixture.

| Change | Headless probe | Rendered probe |
| --- | --- | --- |
| none (control) | exit 0; 12 and 12 | exit 0; 12 of 12 |
| swap removed | exit 1; 0 and 0 | exit 1; 0 of 12 |
| only the first triangle swapped | exit 1; import 1 | exit 1; 1 of 12 |
| every other triangle swapped | exit 1; import 6 | exit 1; 6 of 12 |
| old fixture, fix in place | exit 1; 12 and 6 | exit 0; 12 of 12 |
| old fixture, swap removed (the old state) | exit 1; 0 and 6 | exit 1; 0 of 12 |

The partial-swap rows ran before `facing_normals` existed, so they show only
the import count. The rendered probe measures the host boundary alone, so it
passes on the old fixture. There, the capture's topology check fails instead,
as described above.

Swapping the first and second index instead of the second and third also
passes both probes. Any single swap reverses a triangle, so that change is an
equivalent fix, not a regression.

## Limits

- The headless checks cover the manifest's asset only, the maze tile. They
  compare against surface 0 of Godot's import, the tile's only primitive.
- The rendered check isn't part of `scripts/check.elisascript`, which stays
  runnable without a window server.
- Godot's material `cull_mode` is left at its default everywhere. Nothing here
  makes a cooked material double-sided.

## Validation on 2026-09-22

These ran on Godot 4.7.2 in two detached worktrees on 50b5c2f that held only
this change, because other sessions shared the main working tree.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed after a fresh
  `scripts/cook_assets.py`. The Godot probe printed `triangles=12
  facing_godot_import=12 facing_normals=12`, and both Elisa Proof suites passed
  (17 and 6 obligations, none failed).
- `elisascript scripts/godot_capture.elisascript` passed end to end: the
  winding capture (12 of 12), dimensions, determinism, topology, frame budget,
  the live-input frame and the pattern match against the native frame. The
  native frame was the main tree's `build/wicked-frame.png`. Before the
  fixture fix, the same run failed with one topology mismatch. On 50b5c2f
  without this change, it passed.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal with the fixed
  tile. The smoke cooked the maze's bundle from the new source.
- The rendered probe also passed on Forward+ (Metal 4.0).
- The mutations above behaved as listed.
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- The sibling compiler checkout had uncommitted changes from other work.
  `ELISA_ALLOW_STALE_STAGE1=1` used its existing stage1 binary.
