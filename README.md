# Elisa Engine

An experimental Elisa-native game engine built around algebraic entity hierarchies,
numeric identity, and specialist engine libraries. Godot and a Wicked-based native
backend are planned; neither is implemented yet.

## First milestone: entity identity

`src/entity_id.elisa` supplies a world-local numeric identity allocator. Initialize
`EntityIdAllocator{last_issued: ENTITY_ID_INVALID}` and call `entity_id_allocate(&allocator)`.
Successful allocation returns IDs from 1 through `ENTITY_ID_MAX`.
`ENTITY_ID_INVALID` (zero) means allocation failed. Exhaustion and negative allocator state leave the cursor unchanged.
The initial representation uses positive `i64` values; a packed handle ABI is not fixed.

IDs are never recycled within one allocator lifetime. The future world must own one
allocator and prevent copying, resetting, or rolling back its cursor while IDs survive.
The current public struct does not enforce that ownership. IDs from different worlds
may coincide; these are not persistent asset IDs or globally unique identifiers.
Identity alone does not establish that an entity is still alive or grant storage access.

### Checks

Requires an ElisaScript launcher, the self-hosted Elisa compiler, and a built
Elisa Proof assistant:

```sh
PATH="$HOME/.elisac:$PATH" elisascript scripts/check.elisascript
```

For other installations, set `ELISA_COMPILER_BIN` and `ELISA_PROOF_BIN` to executable
paths. By default the prover is read from the sibling `elisa-proof/build/elisa-proof`.
The script passes tool arguments directly without a shell and returns nonzero on
compilation, runtime test, or proof failure. It saves the proof
report in `build/entity-id-proof.json`. It does not rebuild either toolchain.

- `test/`: executable checks for initial allocation, distinct successive IDs, the
  final valid ID, repeated exhaustion, and invalid allocator state.
- `proof/`: Elisa Proof checks importing the actual implementation. They establish
  that a valid cursor advances by one and produces an ID greater than every earlier
  ID bounded by that cursor. Imported runtime code is checked as well.

These proofs do not yet establish whole-world ownership, liveness, concurrency safety,
or backend correctness. Runtime tests cover exhaustion and state preservation; the
current proof contracts do not claim those mutable-state postconditions.

## Next milestone

Add a minimal Elisa world with the upward-composed entity hierarchy, spawn/despawn,
and checked lookup. Keep identity allocation private to that world. Then synchronize
a small scene into one rendering backend and run the same scene through the second.

### ElisaScript migration status

The shell check has been replaced by `scripts/check.elisascript`. ElisaScript’s
existing process, environment, filesystem, and `Script::source_path()` APIs
cover the workflow; no new process API is needed. The script locates the engine
relative to its own source path, so invocation does not depend on the working
directory. Empty tool overrides use the defaults.

Validated on 2026-09-15 with the native ElisaScript launcher: entity runtime tests
passed, all 15 proof obligations were proved and replayed, and ten comparison cases
matched the original shell workflow. The launcher is installed at
`~/.local/bin/elisascript` on this workstation.

ElisaScript and its native compiler required integration fixes. Build identities,
regressions, and scope are recorded in the sibling
[ElisaScript validation notes](../elisa-script/docs/engine-check-validation.md).
The script relays captured stdout/stderr after each tool exits.

To rerun the comparison with fake tools:

```sh
python3 test/check_workflow.py "$HOME/.local/bin/elisascript"
```

### Tail-return support

The allocator uses implicit function tail returns. The installed compiler and
prover were refreshed and the runtime/proof check passes. Scope, regression
evidence, and rebuild provenance are in
[tail-return validation](docs/tail-return-validation.md).
