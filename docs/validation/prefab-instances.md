# Prefab instance validation

`src/world/prefab.elisa` keeps authoring identity separate from runtime identity. A
versioned definition stores stable authoring IDs, parent links, and optional
visual asset references; each instance receives a caller-owned runtime range, so
two instances cannot collide. Overrides replace a node's local transform or
visual asset reference in one instance without changing the definition or
another instance.

Validation is bounded by `Prefab::MAX_NODES`. Definition loading rejects empty or
invalid versions, duplicate IDs, missing parents, and parent cycles. Instantiation
copies the definition's bindings into an affine instance, and instance validation
rejects invalid or duplicate runtime IDs. The test covers two independent
instances, an override, a missing parent, and a two-node cycle.

`src/world/prefab_world.elisa` maps a definition into the primary checked
`World`, stores each runtime `EntityRef` behind private binding fields, applies
local and visual overrides, and destroys the complete mapping through the
deferred world command boundary. `src/world/prefab_persistence.elisa` snapshots
stable authoring IDs, plain transforms, and optional `Assets::VisualAsset` values
(stable mesh/material IDs plus local bounds). Restoring into a newly spawned
instance reproduces those values without serializing a world epoch or native
handle. The primary-world test reloads into a separate `World` with a fresh
epoch, checks that the old `EntityRef` is invalid there, and verifies that both
the base visual and overridden visual references survive by authoring ID.
`src/world/prefab_scene.elisa` composes up to eight links, validates missing
definitions and parent cycles, spawns nested instances in topological order,
and destroys them in reverse order.

`scripts/scene_file.py` is the durable scene-file boundary. It canonicalizes a
version-1 JSON document through the fsynced save journal, migrates the version-0
shape, validates bounded definitions/links/overrides and parent topology, checks
stable 64-bit mesh/material IDs and ordered finite visual bounds, and rejects
runtime/native handles. The self-test covers round-trip persistence, migration,
cycles, stable visual references, malformed IDs/bounds, and the native boundary.

Run the focused check with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
~/.local/bin/elisascript scripts/check.elisascript
```

The focused primary-world and nested-scene checks are:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.elisac/elisac-stage1" -emit exe -o build/prefab-world-test \
test/prefab_world.elisa && build/prefab-world-test
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
"$HOME/.elisac/elisac-stage1" -emit exe -o build/prefab-scene-test \
test/prefab_scene.elisa && build/prefab-scene-test
```

The primary-world epoch/rebinding and stable visual-reference regressions passed
on 2026-09-27. Native world epochs, backend handles, and GPU resources remain
outside the scene file; a load still requires the Elisa prefab modules to spawn
fresh runtime bindings. Wiring restored visual IDs through the runtime asset
catalogue into renderer resource recreation remains W04 follow-up work.
