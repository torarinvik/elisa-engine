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

Run the focused check with:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$HOME/.elisac/elisac-stage1" \
~/.local/bin/elisascript scripts/check.elisascript
```
