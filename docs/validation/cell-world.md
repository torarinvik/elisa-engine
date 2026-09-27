# Resident world cell payloads

`CellWorld::Runtime` gives each resident cell a primary-world anchor and can activate one bounded `Prefab::Definition` payload beside it. A prefab may contain up to 64 nodes; the world entity limit remains the overall bound on active payload size. Each active cell stores its dependency generation and stable prefab and instance IDs. Callers resolve a node through `cell_prefab_reference` using the prefab's authoring ID; runtime entity references remain tied to the current `World` epoch.

An activation for an already resident cell is idempotent only when its generation and payload identity match. A new dependency generation must first deactivate the current cell, then activate it again with the updated definition. If anchor creation, prefab spawning, or streaming-budget admission fails, activation removes any entities already created and cancels the queued stream request. Deactivation destroys the prefab before its anchor and releases the stream residency. `trim` uses the stream's configured hysteresis and follows the same teardown path.

The integration test `test/cell_world.elisa` exercises payload activation, lookup by authoring ID, idempotent repeat activation, stale-generation rejection, reactivation after unload, budget-failure rollback, trimming, and teardown. Run it directly with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/runtime/elisacore_runtime.o" \
../Elisa-compiler/bin/elisac-stage1 -emit exe -o build/cell-world-test test/cell_world.elisa
build/cell-world-test
```

This is a synchronous world-activation boundary. A04 already provides native asynchronous decode/upload scheduling and renderer residency budgets, but cell-owned requests/releases are not yet wired to that loader. Recreation of native mesh/material resources from stable catalogue IDs also remains open; the cell payload currently preserves prefab data and stable visual references only.
