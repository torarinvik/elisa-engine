# Prefab instance validation

`src/world/prefab.elisa` keeps authoring identity separate from runtime identity. A
versioned definition stores stable authoring IDs and parent links; each instance
receives a caller-owned runtime range, so two instances cannot collide. Overrides
replace a node's local transform in one instance without changing the definition
or another instance.

Validation is bounded by `Prefab::MAX_NODES`. Definition loading rejects empty or
invalid versions, duplicate IDs, missing parents, and parent cycles. Instantiation
copies the definition's bindings into an affine instance, and instance validation
rejects invalid or duplicate runtime IDs. The test covers two independent
instances, an override, a missing parent, and a two-node cycle.

`src/world/prefab_world.elisa` maps a definition into the primary checked
`World`, stores each runtime `EntityRef` behind private binding fields, applies
local overrides, and destroys the complete mapping through the deferred world
command boundary. `src/world/prefab_persistence.elisa` snapshots only stable
authoring IDs and plain transforms; restoring into a newly spawned instance
reproduces the override without serializing a world epoch or native handle.
`src/world/prefab_scene.elisa` composes up to eight links, validates missing
definitions and parent cycles, spawns nested instances in topological order,
and destroys them in reverse order.

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

Full durable scene serialization beyond the plain override snapshot remains a
future save-schema integration.
