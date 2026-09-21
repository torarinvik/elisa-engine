# Elisa Maze

The interactive native client builds gameplay entities in the Elisa `World`,
extracts a bounded render snapshot, and submits it through the persistent
SDL3/Metal scene service. It does not read `backends/scene_manifest.txt`.

Build and run it from the engine root:

```sh
python3 scripts/elisa_build_run.py run --project examples/maze
```

The project runner cooks two indexed bundles before building. First it stores
the brick wall image `assets/maze_wall.png` as the `wallalbedo` section of
`assets/maze_textures.elpk`. Then it cooks `assets/maze_tile.gltf` to
`assets/maze_tile.elpk`, whose manifest names `maze_textures.elpk` as a
dependency. The runtime refuses to load the tile's mesh unless that dependency
is present. The tile bundle stores the normalized mesh in a bounded, aligned,
checksummed `mesh` section; the native loader still accepts loose `.pkg`
geometry packages. The runtime geometry cooker accepts one untransformed
static mesh node with up to 16 indexed triangle primitives. Each primitive
has POSITION and optional NORMAL and TEXCOORD_0 streams, and missing normals
are generated. Each primitive becomes a material subset. A document may
declare up to 16 materials, and a primitive's `material` index picks the slot
its subset draws with. At runtime a snapshot row names a material set with one
registered material per slot, or a single material for every slot. The
maze tile has one primitive and no materials, so it has one slot. The cooker
does not import material properties yet, so a material with anything beyond a
name fails. It also rejects node transforms, skins, morph targets and other
vertex attributes instead of silently dropping them. The maze's PBR materials
are authored separately in Elisa; the wall material takes its base color from
the `wallalbedo` section.

The client requests both bundles from the render scene's asset worker and
keeps presenting frames with a "Loading maze assets" overlay until the mesh and
texture are resident. It then registers the materials and builds the maze. A
bundle that fails to load, or loading that takes more than 30 seconds, exits
with status 17.

Press **Space** to start, use the arrow keys or **WASD** to move, press **P** to
pause or resume, **R** to restart, and **Escape** or the window close button to
quit.

The native gate builds `native_smoke_main.elisa` through the same project runner.
That finite entry point opens a hidden host, presents a scripted win and restart
through the native snapshot transaction, verifies entity counts, then exits.
