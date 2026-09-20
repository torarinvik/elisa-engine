# Elisa Maze

The interactive native client builds gameplay entities in the Elisa `World`,
extracts a bounded render snapshot, and submits it through the persistent
SDL3/Metal scene service. It does not read `backends/scene_manifest.txt`.

Build and run it from the engine root:

```sh
python3 scripts/elisa_build_run.py run --project examples/maze
```

Press **Space** to start, use the arrow keys or **WASD** to move, press **P** to
pause or resume, **R** to restart, and **Escape** or the window close button to
quit.

The native gate builds `native_smoke_main.elisa` through the same project runner.
That finite entry point opens a hidden host, presents a scripted win and restart
through the native snapshot transaction, verifies entity counts, then exits.
