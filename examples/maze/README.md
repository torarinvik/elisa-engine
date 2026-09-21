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
geometry packages. This first runtime geometry cooker accepts a single static
mesh node with one indexed triangle primitive and POSITION, NORMAL, and
optional TEXCOORD_0 streams. It rejects node transforms, skins, morph targets,
source material bindings, and unsupported vertex attributes instead of
silently dropping them. The maze's PBR materials are authored separately in
Elisa; the wall material takes its base color from the `wallalbedo` section.

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
