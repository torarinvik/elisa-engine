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
limit failures return an error without exposing a partial mesh. The shared
workspace currently contains only the synthetic FBX fixture; the external game
asset checks below cannot be rerun without the WallGame asset tree.

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

When those assets are present, the optional test checks that the walking and
running files share the same 34-bone name order, expected clips and durations
are visible, the character mesh is selected instead of the auxiliary sphere,
and the large fence file parses under the configured limits. That asset-tree
path remains unverified in this checkout. The synthetic triangle checks unit
conversion, node translation, finite generated normals, and valid indices.

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
repeated output was byte-identical. The sibling worktree records the 3,077,694-
triangle Arc Gate reduced to 12,000 triangles, 10,009 vertices and 833,098 bytes
with tangents at 0.00124 relative error. That external asset tree is absent from
this checkout, so the dense cook was not independently rerun here. For a real
source, supply `SOURCE --asset-path PROJECT_RELATIVE_PATH --output
DESTINATION.pkg`; the asset key must be safe and project-relative.

The sibling worktree also cooked its supplied walking FBX with a 34-bone rig
and 97,679 deduplicated vertices into an 11,755,338-byte package. Its importer
test checked finite, normalized four-bone influences, and `--cooked-skin`
validates those streams through the bounded runtime package reader. The source
asset tree is not present in this checkout, so that asset-specific result could
not be repeated here.

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
Wicked's PBR path, but the cooker does not discover FBX material maps. The real
walking/running/fence FBX assets have not been cooked or rendered in this
checkout. Shared mesh residency, source texture mapping, and asynchronous asset
residency remain.
