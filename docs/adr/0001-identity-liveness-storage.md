# ADR 0001: Identity, liveness, and storage separation

**Status:** accepted (2026-09-17)

## Context

A numerical ID alone cannot prove liveness, storage location, or which
world issued it. Recycling an ID while references survive breaks every
backend mapping built on top of it.

## Decision

- Entity IDs are positive world-local `i64`, starting at one; zero is
  unissued, never a failure result. IDs are never recycled in one
  allocator lifetime; a failed spawn burns its ID rather than rewinding.
- The allocator is affine and world-owned; negative cursors and exhaustion
  are explicit errors that leave the cursor unchanged.
- References carry a world epoch (`EntityRef{world_epoch, id}`); lookup
  checks the live registry, so cross-world and dead references fail.
- Despawn removes liveness first; backend cleanup may finish later.
- Rollback snapshots keep the allocator high-water mark outside reversible
  state rather than rewinding the cursor.

## Evidence

`src/entity_id.elisa`, `src/world/world.elisa`,
`src/world/reference.elisa`, `test/world.elisa` (churn, compaction,
capacity, corruption), `proof/entity_id.elisa` (advance-by-one, no-reuse
conditional on a monotonic cursor), `src/net/replication.elisa`
(high-water in rollback snapshots).

## Not covered

Whole-world ownership/concurrency proofs; persistent cross-build identity
lives in asset IDs, not entity IDs; packed-handle ABI not fixed.

## Root motion drives movement (2026-09-18)

`examples/maze/rootmotion.elisa` closes the plan's requirement that root motion
drive the character through the movement policy rather than compete with a
second controller. The walker samples a clip whose root advances a fixed
distance per cycle, adds the sampled root translation to its travelled
distance, and commits a cell only when that distance reaches the cell length.
`test/maze.elisa` pins the consequence: a cell-length clip commits one cell per
eight-tick cycle, while a half-length clip needs two cycles, so the clip's data
(not a timer) decides movement.
