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
  exports, then reads an SDL key event, maps it to a move code, and calls
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
