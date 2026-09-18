# ADR 0013: the Godot host embeds Elisa gameplay through a hand-written GDExtension

## Status

Accepted.

## Context

ADR-0012 gave the game a C ABI and the native host links it, so input reaches
Elisa gameplay in-process. The Godot host still consumed only the fixture and
synthetic events. The plan's Unity analogue needs the same in-process path in
Godot, otherwise the two backend families have different authority over the
game.

`godot-cpp` is the usual bridge, but it publishes no branch or tag for the
Godot installed here (4.7.2; the newest is 4.5), so holding the host back or
carrying a patched binding would tie the engine to a second source tree. Godot
can dump its own C interface header (`--dump-gdextension-interface`), which
makes a direct binding against the version-matched ABI the smaller long-term
surface: one C++ file the engine owns, no external binding layer, and a header
that always matches the running binary.

## Decision

- `scripts/fetch_gdextension_header.py` asks the installed Godot for its own
  `gdextension_interface.h` (a temporary project with `--dump-gdextension-interface`)
  and writes it to `dependencies/gdextension/`.
- `backends/godot-embed/elisa_godot_bridge.cpp` registers an `ElisaMaze`
  class that calls the `libmaze` exports directly, caching the interface
  function table once at `gdextension_init` and interning its StringNames at
  scene-level initialization.
- `ElisaMaze` is **abstract with static methods**. The Elisa session is
  process-global (the exports operate on one `global mutable` game), so
  offering `ElisaMaze.new()` would imply per-object state that does not exist.
  A static facade is the honest shape, and it also avoids hand-allocating an
  engine `Object` from C: `create_instance_func` must return a real Godot
  object constructed through `classdb_construct_object3` with
  `object_set_instance` and a post-initialize notification.
- Every boundary value is an INT Variant (int64); narrowing to the C ABI's
  int32 happens at the call, where the values are small by construction.
- `backends/godot-embed/{project.godot,elisa_maze.gdextension,godot_embed_probe.gd}`
  plus `scripts/godot_embed_probe.py` emit the archive, build the extension,
  import it, and run a full session (start, movement, hazard, reset, win) that
  must agree with the native embedding.

Rules this fixes:

- The bridge is versioned by the header the installed Godot dumps, not by a
  separate binding release; upgrading Godot re-dumps the header.
- Godot loads `.gdextension` files only from the generated editor cache
  (`res://.godot/extension_list.cfg`), so the runner runs `--import` first and
  validates that file. This Godot build crashes at import shutdown (exit 134)
  after writing the cache, so the artifact is validated, not the exit code.
- Godot exits 0 even when a `--script` probe fails to parse or run, so the
  runner requires the session's success marker in the output rather than the
  exit status alone.
- If the extension is missing, the probe fails with an explicit error instead
  of silently passing.

## Consequences

- Both host families now drive the same Elisa gameplay through the same Elisa
  C ABI; the fixture remains the rendering contract.
- The bridge stays small because gameplay, rules, and state are still in Elisa;
  adding an export means adding one `export fn` and one bound method.
- When `godot-cpp` ships a matching branch, the `.gdextension` can be
  re-pointed at a generated binding without changing the Elisa side, because
  the boundary is the C ABI, not the binding technology.
- The Godot host no longer needs synthetic input replay to reach gameplay; only
  its headless capture path still uses synthetic events.
