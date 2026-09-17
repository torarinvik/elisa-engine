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
