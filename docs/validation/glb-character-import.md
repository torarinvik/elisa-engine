# Skinned GLB character import

`scripts/cook_glb_asset.py` adds a project-level `glb` asset cooker for
skinned character models. It imports a GLB through Blender, exports its rigged
mesh through the existing bounded FBX cooker, and can transfer clips from an
FBX animation source when that source contains every target joint name. Each
source clip becomes an independent NLA take in the cooked Elisa package.

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
armature. A source may contain up to eight non-static clips. GLB input must
contain one armature and at least one skinned child mesh. The existing cooked
geometry format keeps one mesh, one skin hierarchy, and up to eight clips; it
does not yet retain multiple material subsets or a separate map for every PBR
channel. A single material's base-color image can be assigned through Elisa.

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
