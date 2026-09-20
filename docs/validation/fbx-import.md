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
mesh. Current game assets remain under these limits, including the 3,077,694-
triangle fence model.

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

The test confirms that the walking and running files share the same 34-bone
name order, that their expected clips and durations are visible, that the
character mesh is selected instead of the auxiliary sphere, and that the large
fence file parses under the configured limits. The synthetic triangle checks
unit conversion, node translation, finite generated normals, and valid indices.

This stage only parses metadata and decodes one mesh. It does not yet preserve
the full node hierarchy, material subsets or texture paths, skin weights or
bind poses, or animation curves. It does not emit the engine's normalized
package and is not connected to an Elisa asset-load or render call. A05/C01
integration and real Wicked rendering remain the A09 completion criteria.
