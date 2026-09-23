# GLB mesh and character import

`scripts/cook_glb_asset.py` adds a project-level `glb` asset cooker for static
and skinned models. It imports a GLB through Blender and writes geometry
through the existing bounded FBX cooker. Static GLB meshes can use
`max_triangles` to reduce dense Blender geometry below the FBX parser's memory
ceiling and configured triangle budget before conversion; meshoptimizer still
handles safe residual simplification in the FBX cooker. Skinned meshes
can transfer clips from an FBX animation source when that source contains
every target joint name. Each source clip becomes an independent NLA take in
the cooked Elisa package.

Clip transfer bakes world-space bone rotations and root translations onto the
target rig. It preserves the GLB armature's object scale and target bone lengths;
source object-transform channels are never attached to the target. This prevents
a source rig at scale 1 from replacing a GLB's required 0.01 unit conversion.
The rigs must have compatible bone-axis conventions in addition to matching
names; this is not a general humanoid retargeter or a foot-contact solver.

The cooker can also extract the first material's base-color PNG or JPEG image
from an embedded GLB bufferView, data URI, or project-local image URI. Texture
extraction is opt-in. The game then assigns the generated image through
`RenderScene::set_texture`; the game project contains no native import shim.

Declare the inputs and generated outputs in `elisa.project.json`:

```json
{
  "importer": "glb",
  "source": "assets/character.glb",
  "asset_path": "assets/character.glb",
  "output": "build/cooked/character.pkg",
  "animation_source": "assets/locomotion.fbx",
  "texture_output": "build/cooked/character-basecolor.png"
}
```

The optional animation source must be FBX with the same joint names as the GLB
armature. A source may contain up to twenty-four non-static clips. GLB input must
contain one armature and at least one skinned child mesh. The existing cooked
geometry format keeps one mesh, one skin hierarchy, and up to twenty-four clips; it
does not yet retain multiple material subsets or a separate map for every PBR
channel. A single material's base-color image can be assigned through Elisa.
Static GLB input may contain one or more mesh nodes, but the current FBX cooker
retains its largest mesh. Their node transforms are preserved through the
Blender-to-FBX conversion. Triangle simplification applies to static GLBs;
the cooker rejects triangle limits for skinned meshes because remapping bone
influences during simplification is not yet supported. The limit is between 1
and 1,000,000 triangles.

Blender is required only during asset cooking. The cooker looks for a
`BLENDER` executable path, a `blender` command on `PATH`, or the standard macOS
application location. Blender 5.2.2 was used for the supplied GLB. The
generated FBX and package are temporary/cooked build outputs; the source GLB
remains the project asset.

The GLB container reader has a deterministic synthetic self-test:

```sh
python3 scripts/cook_glb_asset.py --self-test
```

`python3 scripts/test_elisa_build_run.py` checks that the project runner routes
GLB declarations to this cooker, resolves the animation and texture paths
inside the project, and rejects invalid extensions and path escapes. An
asset-root integration check should also cook a real skinned GLB, confirm the
expected clip names and texture output, then build the game through
`scripts/elisa_build_run.py`.

Run the Blender regression with:

```sh
blender --background --python-exit-code 1 --python scripts/test_glb_retarget_blender.py
blender --background --python-exit-code 1 --python scripts/test_glb_static_reduction_blender.py
python3 scripts/test_glb_static_cook.py
```

The retarget test transfers two clips between rigs with a 100x object-scale
difference and different child rest orientations, checking root motion, world
rotations, preserved limb length, and absence of object-transform animation
keys. The static-mesh test reduces a generated dense grid under a configured
triangle ceiling and checks regenerated normals and unchanged node transforms.
The end-to-end static cook test exports a transformed dense Blender mesh to GLB,
cooks it through the production Blender and FBX stages, and verifies the triangle
bound, package stream lengths, index bounds, and finite unit normals.
On macOS with Blender 5.2.2, `python3 scripts/test_glb_static_cook.py` passed:
the 3,042-triangle fixture cooked to 55 triangles and 37 vertices under its
64-triangle ceiling. This validates package output and geometry streams; it does
not establish visual simplification quality or runtime LOD behavior.
The supplied cyborg cooks to 1.6383 metres tall (previously 163.83 after the
incorrect raw-action transfer). All six locomotion clips were sampled at their
start, middle and end, retaining plausible character dimensions.

The FBX normalization step also reconstructs the rig's rest hierarchy from
skin-cluster bind matrices. FBX default node transforms may contain a posed
animation frame, so treating them as the bind pose deforms the new mesh
incorrectly. A native fixture covers a distinct default/bind pose with a scaled
ancestor. Comparing the cooked cyborg's CPU-skinned idle vertices to Blender's
evaluated mesh gives a maximum surface-position difference below 0.000001 m.
