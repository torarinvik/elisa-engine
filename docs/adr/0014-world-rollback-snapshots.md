# ADR 0014: World rollback snapshots preserve allocator high-water

**Status:** accepted (2026-09-19)

## Context

Prediction and replay need to restore gameplay state without reusing an entity
identity. A snapshot that rewinds the allocator would make a surviving
`EntityRef` ambiguous after a despawn and replay.

## Decision

`WorldRollback::Snapshot` stores the epoch, registry, dense live-ID column, and
typed actor/enemy storage. `capture` accepts only a valid world, and `restore`
checks the epoch and the internal registry/column/storage invariants before
copying state. The allocator is owned by `World` and is intentionally absent
from the reversible snapshot, so its high-water mark continues forward after
restore.

The snapshot is a value used during an explicit rollback phase. It does not
provide concurrent access or a whole-world ownership proof; those remain
separate runtime and compiler concerns.

## Evidence

`src/world/rollback.elisa` and `test/world.elisa` cover capture, despawn and
restore, continued monotonic allocation, and typed storage consistency. The
identity provenance proof remains linked to `src/world/reference.elisa` and
`proof/world.elisa`.
