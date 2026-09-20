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

Input is bounded to 512 MiB per file, 512 MiB temporary parsing memory, 1.5
GiB parsed scene memory, 4,096 nodes and bones, 1,024 meshes and materials,
256 animation stacks, 5,000,000 scene triangles, 512 MiB extracted corner data,
and 256 MiB for vertex indexing. Parsing is strict; external files are not read.
Malformed input and limit failures return an error without exposing a partial
mesh. This checkout's validation used the synthetic fixture only; the optional
game-asset checks below were not run because those FBX files were unavailable.

Run the synthetic cm-unit triangle fixture with:

```sh
python3 scripts/fetch_dependencies.py --only ufbx_source
python3 scripts/fetch_dependencies.py --only ufbx_header
python3 scripts/fetch_dependencies.py --only ufbx_license
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
float32 positions/normals/UVs, uint32 indices, bounds, and fixed strides. It
validates all decoded lengths, finite values, indices, and the runtime reader's
64 MiB package/16 MiB section limits before reporting success. Run
`python3 scripts/cook_fbx_asset.py --self-test` for the synthetic package test.
This revision's self-test cooked one normalized triangle. For a real source,
supply `SOURCE --asset-path PROJECT_RELATIVE_PATH --output DESTINATION.pkg`;
the asset key is stored in the package and must be a safe relative path. Real
asset cooking was not exercised in this checkout.

`native/package_load.h` now reads the optional UV channel and copies it into
Wicked's first UV set. A synthetic native-reader package fixture checks UV
preservation; `native/cooked_geometry_package.h` separately validates and
decodes cooked geometry for the public `RenderScene::create_mesh` API. The
runner accepts project `asset_cooks` declarations and invokes the cooker before
building. Its automated test validates project-contained paths and cooker
invocation, while the native probe tests geometry decoding with the synthetic
triangle package. The SDL3/Metal native smoke cooks that triangle and verifies
it renders through `RenderScene::create_mesh`, rejects traversal, absolute, and
symlink-escape paths, and checks handle cleanup. Real game FBX assets were not
cooked or rendered because they are absent from this checkout.

Import and cooking still select one mesh. They do not preserve the full node
hierarchy, material subsets or texture paths, skin weights or bind poses, or
animation curves. Runtime loading/rendering now works for normalized cooked
packages; the real game-asset path and A05/C01 integration remain unverified.
