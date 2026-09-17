# ADR 0003: Upward-composed algebraic hierarchies

**Status:** accepted (2026-09-17)

## Context

A conventional OOP base-class tree forces every entity into one immense
renderable/movable/damageable/serializable inheritance chain, mixing
taxonomy with capabilities.

## Decision

- Entity kinds compose upward: `Actor` declares membership in `Entity`,
  `Enemy` in `Actor`; the parent denotes the resulting set of cases.
- Hierarchies represent what an entity is and support typed matching.
  Protocols express behaviors shared by unrelated types. Ordinary
  composition carries associated data (transforms, health, emitters).
- Capabilities (renderable, movable, damageable) are data relationships,
  not ancestry. Source-level extension adds descendant cases; the final
  linked program closes the set for exhaustiveness checking.

## Evidence

`src/world/world.elisa` (`Entity`/`Actor`/`Enemy` with `ActorRecord` and
`EnemyRecord` stores), `test/world.elisa` leaf matching, maze game rules
in `examples/maze/game.elisa` matching on moves rather than a type tree.

## Not covered

Runtime-loaded binary plugins with unknown cases remain incompatible
with compile-time exhaustiveness; that combination is not promised.
