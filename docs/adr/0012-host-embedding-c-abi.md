# ADR 0012: hosts embed Elisa gameplay through the C ABI

## Status

Accepted.

## Context

ADR-0011 made `backends/scene_manifest.txt` the boundary between Elisa and the
backends. That channel is one-directional data: a host can render Elisa's state
but cannot feed input back into gameplay, so a keypress could not drive the
game. The plan's input direction ("SDL3 … Elisa action mapping") and its Unity
analogue need a two-way boundary where a host runs input and asks Elisa to
advance.

## Decision

Export the game's public entry points as a **C ABI** with the compiler's
`-emit c-archive`, and let a host link that archive and call it in-process:

- `examples/maze/capi.elisa` holds a `global mutable` game and exports
  `maze_start`, `maze_step(direction)`, `maze_player_x/y`, `maze_status`, and
  `maze_lives`. The archive bundles the Elisa runtime, so the host links one
  library.
- `native/embed_probe.cpp` links the archive, drives gameplay through the
  exports, then reads an SDL3 key event, maps it to a move code, and calls
  `maze_step` — live input reaching Elisa gameplay.
- `scripts/embed_probe.py` emits the archive, builds the host, and runs it.

Rules this fixes:

- Exports must be **scalar in, scalar out**; a qualified export target
  (`Module::function`) is required, not a name brought in by `using`.
- The C archive is the host-facing contract; its generated header is an audit
  artifact, and `libmaze.elisa-abi.json` records the exported surface.
- Gameplay state stays in Elisa; the host owns only device input and the loop
  that calls the exports.

## Consequences

- A host can drive the game with live input while Elisa still owns identity,
  liveness, rules, and state; the fixture remains the rendering contract.
- The two boundaries are complementary: the fixture carries state out, the C
  ABI carries input in.
- Exporting through a C ABI is now a supported path; a new game surface adds
  `export fn` wrappers rather than host-side reimplementations.

## Live input drives both rendered hosts (2026-09-18)

Both capture hosts rendered replayed routes; now each also renders state it
advanced itself. The native side (`native/live_game_probe.h`) maps a real SDL
key event to the portable move code, calls `maze_step` through the archive,
requires the queried player cell to match (2,1), positions the rendered object
there, and saves a second frame; the runner emits the archive before compiling
the probe and verifies the live frame with `compare_renders.py nonblank` (range
0.8353). The Godot side builds the same GDExtension into the capture project
(`scripts/build_godot_extension.py`), and `backends/godot/capture.gd` parses a
synthetic key, calls `maze_step`, places a live marker at the queried cell, and
saves a verified live frame after the fixture frame, so determinism and
topology comparisons stay unchanged. A host that renders nothing fails in both
runners.

## c-archive omits a nested-module function (2026-09-18, compiler defect)

Extending the C ABI with a query surface (`maze_is_wall`, `maze_wall_count`)
inside a `module` wrapper made `-emit c-archive` produce an archive whose object
referenced `_Maze::Steps.next_position` from `maze_try_move` without emitting its
definition, so linking failed with an undefined symbol. Top-level functions and
a top-level `global mutable` emit reliably, so the export surface is now flat;
the compiler defect remains for a module-wrapped file and should be filed in the
compiler repository. Per the plan's guidance, the engine did not move gameplay
policy into C++ to work around it: the game stays in `examples/maze`, and only
the export surface was flattened.
