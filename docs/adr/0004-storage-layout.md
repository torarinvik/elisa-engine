# ADR 0004: Public algebraic model over data-oriented stores

**Status:** accepted (2026-09-17)

## Context

One slot sized for the largest case of the root enum wastes memory and
cache; separate component copies of one gameplay field disagree.

## Decision

- Public API stays algebraic while bulk data lives in a live registry
  plus shared hot stores, kind-specific stores, and subsystem stores.
- Exactly one authoritative copy of each gameplay field exists.
- Checked lookup yields a borrow valid for a defined access phase;
  structural mutation needs exclusive access or defers spawn/despawn to
  an explicit commit phase. The current serial fixed-capacity world is
  the first instance, not the final layout.

## Evidence

`src/world/world.elisa` (registry rows, actor/enemy stores, compaction,
`world_is_valid` cross-checks), `test/world.elisa` (churn, compaction,
corruption detection).

## Not covered

Measured column/pool migration, persistent storage, parallel access;
serial semantics only.

## Compact live-entity column (2026-09-18)

The plan's storage target moves hot iteration onto compact columns. `World` now
maintains a dense `live_ids` column beside the registry: spawn appends, despawn
removes by shift, and compaction rebuilds it from the live registry rows, so
iterating live entities is O(live) rather than O(registry capacity).
`world_live_column_valid` checks the column holds exactly the live ids once each.
`test/world.elisa` pins it through churn and compaction, and the test records an
honest subtlety: because spawn reuses a freed registry slot, the column order
follows the registry, not spawn order, so the assertions check membership rather
than position.

Two language notes from this work: a nested value block must bind its result to a
local before use, and a primitive array field sized by a `Module::CONSTANT` path
was mis-parsed (the field read as a scalar), so the bound is written as a
literal.
