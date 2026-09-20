# Bounded FBX import

The engine pins ufbx v0.23.0 at commit
`fcc5d6ba444cfd3eb80677dba5e37e493941abe5`. The fetch script verifies SHA-256
for the C source, public header, and bundled license before placing them under
the ignored `dependencies/ufbx/` directory.

`native/fbx_asset_import.h` currently provides a native engine boundary for
strict FBX parsing and a first geometry decode. It requests right-handed +Y-up
coordinates and metres, generates missing normals, ignores embedded textures,
and does not load external files. It reports scene counts, source units, rig
bone-name identity, material texture-reference counts, and the first animation
stack's name and duration. When geometry decode is requested, it selects the
largest triangle mesh and returns indexed positions, normals, UVs, and bounds.
This keeps helper meshes such as the cyborg's small `Icosphere` out of the
current character geometry check.

Input is bounded to 512 MiB per file, 1.5 GiB temporary parsing memory, 3 GiB
parsed scene memory, 4,096 nodes and bones, 1,024 meshes and materials, 256
animation stacks, 5,000,000 scene triangles, 1 GiB extracted corner data, and
768 MiB for vertex indexing. These are lazy hard ceilings for offline asset
cooking, raised after the supplied dense gate exceeded the original decode
budget. Parsing is strict; external files are not read. Malformed input and
limit failures return an error without exposing a partial mesh. The WallGame
asset tree is available in the sibling game checkout; pass its path to rerun
the walking/running/fence checks below.

Run the synthetic cm-unit triangle fixture with:

```sh
python3 scripts/fetch_dependencies.py --only ufbx_source
python3 scripts/fetch_dependencies.py --only ufbx_header
python3 scripts/fetch_dependencies.py --only ufbx_license
python3 scripts/fetch_dependencies.py --only meshoptimizer_simplifier
python3 scripts/test_fbx_import.py
```

To include the supplied game assets, pass their root directory:

```sh
python3 scripts/test_fbx_import.py --assets-root "/path/to/amazing labyrinth/assets"
```

The test confirms that the walking and running files share the same 34-bone
name order, that their expected clips and durations are visible, that the
character mesh is selected instead of the auxiliary sphere, and that the large
fence file parses under the configured limits. The synthetic triangle checks
unit conversion, node translation, finite generated normals, and valid indices.

The engine-owned `scripts/cook_fbx_asset.py` writes the selected mesh into the
existing `elisa-cooked-v2` geometry package with source identity and hash,
float32 positions/normals/UVs, uint32 indices, bounds, and fixed strides. It
can simplify through pinned meshoptimizer with `--max-triangles COUNT`, compacts
unreferenced vertices, and validates lengths, finite values, indices, and the
runtime reader's 64 MiB package/16 MiB section limits. The supplied Arc Gate
cooks from 3,077,694 to 12,000 triangles, 10,009 vertices and 619,538 bytes at
0.00124 relative error. `python3 scripts/cook_fbx_asset.py --self-test` also
passed with a generated 512-triangle grid reduced to 128 triangles and 97
vertices at 0.00003 relative error; repeated output was byte-identical. For a
real source, supply `SOURCE --asset-path PROJECT_RELATIVE_PATH --output
DESTINATION.pkg`; the asset key must be safe and project-relative.

`native/package_load.h` reads the optional UV channel and copies it into Wicked's
first UV set. `native/cooked_geometry_package.h` decodes bounded runtime packages
for the public `RenderScene::create_mesh` API. The project runner accepts
`asset_cooks` declarations and forwards optional triangle limits. Runner tests
cover project-contained paths and cooker invocation; the native package probe
checks geometry decoding. The SDL3/Metal smoke cooks and renders the synthetic
triangle through `RenderScene::create_mesh`, rejects traversal, absolute and
symlink-escape paths, and checks handle cleanup. The public scene API also has
emission, bloom, and texture assignment controls; see the separate
[`render-scene texture evidence`](render-scene-textures.md) note.

Import and cooking still select one mesh. They do not preserve the full node
hierarchy, material subsets or texture paths, skin weights or bind poses, or
animation curves. Elisa now assigns the Arc Gate base-color map explicitly
through `RenderScene::set_texture`, but the cooker still does not discover FBX
material maps or carry tangent frames for normal mapping. Shared mesh residency,
junction variants, and electric particles remain.
