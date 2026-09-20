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
For a single skin deformer it also returns cluster bone names in index order
and four normalized influences per cooked vertex. Multiple skin deformers and
rigs over 64 clusters fail with a clear import error. Vertex deduplication
includes influences, and the cooker rejects simplification of skinned geometry
until it can remap weights safely. This keeps helper meshes such as the
cyborg's small `Icosphere` out of the current character geometry check.

Input is bounded to 512 MiB per file, 1.5 GiB temporary parsing memory, 3 GiB
parsed scene memory, 4,096 nodes and bones, 1,024 meshes and materials, 256
animation stacks, 5,000,000 scene triangles, 1 GiB extracted corner data, and
768 MiB for vertex indexing. These are lazy hard ceilings for offline asset
cooking, raised after the supplied dense gate exceeded the original decode
budget. Parsing is strict; external files are not read. Malformed input and
limit failures return an error without exposing a partial mesh. The supplied
game assets are available in the sibling Amazing Labyrinth checkout at
`../amazing labyrinth/assets`, so asset-specific checks can be rerun there.

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

The asset-root test checks that the walking and running files share the same
34-bone name order, expected clips and durations are visible, the character mesh
is selected instead of the auxiliary sphere, and the large fence file parses
under the configured limits. It passed against the supplied assets on
2026-09-20, together with the cooked skin package check:

```sh
DEVELOPER_DIR="$(xcode-select -p)" python3 scripts/test_fbx_import.py \
  --assets-root "../amazing labyrinth/assets" \
  --cooked-skin "../amazing labyrinth/build/cooked/cyborg-walking.pkg"

walking/running: 83,522 triangles, 34 bones, 2 clips each
fence: 3,077,694 triangles parsed within the configured limits
cooked skin package: 34 bones, 97,679 vertices
```

The synthetic triangle checks unit conversion, node translation, finite
generated normals, and valid indices.

The engine-owned `scripts/cook_fbx_asset.py` writes the selected mesh into the
existing `elisa-cooked-v2` geometry package with source identity and hash,
float32 positions/normals/UVs/tangents, uint32 indices, bounds, and fixed
strides. It can simplify through pinned meshoptimizer with `--max-triangles
COUNT`, compacts unreferenced vertices, and validates lengths, finite values,
indices, and the runtime reader's 64 MiB package/16 MiB section limits. Tangents
are generated from the final simplified geometry and checked for unit length,
normal orthogonality, and handedness. The native reader accepts older cooked
packages without the optional tangent stream and validates it when present.

`python3 scripts/cook_fbx_asset.py --self-test` passed with a generated
512-triangle planar grid simplified to 128 triangles and 97 vertices at 0.00003
relative error; tangent frames passed unit-length and orthogonality checks, and
repeated output was byte-identical. Previous cooks reduced the sibling game's
3,077,694-triangle Arc Gate to 12,000 triangles and 10,009 vertices (619,538
bytes without tangents; 833,098 bytes with tangents) at 0.00124 relative error.
A fresh full game build on 2026-09-20 first exposed an index-memory failure:
static corners carried four unused bone indices and weights. `FbxVertex` now
stores only position, normal and UV data; skinned meshes pass influences as a
second `ufbx_generate_indices()` stream. The engine checkout then built the
supplied game project with:

```sh
DEVELOPER_DIR="$(xcode-select -p)" \
  python3 ../amazing-labyrinth-engine/scripts/elisa_build_run.py build \
  --project "../amazing labyrinth"
```

The runner cooked and validated the 83,442-triangle walking mesh and simplified
the 3,077,694-triangle Arc Gate to 12,000 triangles, 10,009 vertices, and
833,098 bytes at 0.00124 relative error, then compiled and linked the game.
`python3 scripts/cook_fbx_asset.py --self-test` also passes with deterministic
512-to-128-triangle simplification. For a real source, supply
`SOURCE --asset-path PROJECT_RELATIVE_PATH --output
DESTINATION.pkg`; the asset key must be safe and project-relative.

The walking package contains a 34-bone rig and 97,679 deduplicated vertices;
the importer test checked finite, normalized four-bone influences, and
`--cooked-skin` validated those streams through the bounded runtime package
reader.

`native/package_load.h` reads the optional UV channel and copies it into Wicked's
first UV set. `native/cooked_geometry_package.h` decodes bounded runtime packages
for the public `RenderScene::create_mesh` API, including optional skin names and
four influence streams with bone-index and normalization checks. The project runner accepts
`asset_cooks` declarations and forwards optional triangle limits. Runner tests
cover project-contained paths and cooker invocation; the native package probe
checks geometry decoding. The SDL3/Metal smoke cooks and renders the synthetic
triangle through `RenderScene::create_mesh`, rejects traversal, absolute and
symlink-escape paths, and checks handle cleanup. The public scene API also has
emission and bloom controls, with its own validation note.

Import and cooking still select one mesh. They do not preserve the full node
hierarchy, material subsets or texture paths, bind poses, or animation curves.
The cooker and bounded reader now preserve skin names and influences, but the
render uploader still ignores those streams and consumes skinned packages as
static geometry. Tangents make explicitly assigned normal maps usable through
Wicked's PBR path, but the cooker does not discover FBX material maps. The game
currently renders the cyborg package statically and renders a reduced Arc Gate
mesh in its live scene; source texture and normal-map appearance still need
review. Shared mesh residency, material subsets, and asynchronous asset
residency remain.
