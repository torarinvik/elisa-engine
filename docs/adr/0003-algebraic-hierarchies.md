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
in `examples/maze/game.elisa` matching on moves rather than a type tree,
and the `Geometry::Distant` protocol with `test/geometry.elisa`.

## Not covered

Runtime-loaded binary plugins with unknown cases remain incompatible
with compile-time exhaustiveness; that combination is not promised.


## Protocols express behavior, not taxonomy (2026-09-18)

The first engine protocol is `Geometry::Distant`: a point (`Vec3`), an
axis-aligned box (`Bounds3`), and a ray (`Ray3`) all answer
`distance_to(point)` while sharing no representation, no base type, and no
storage. `within_radius(shape: Distant, ...)` and
`nearest_distance[A: Distant, B: Distant](...)` are generic over the protocol,
and the second takes two independently specialized implementing types in one
call. `test/geometry.elisa` pins the three implementations and both generic
forms.

Two compiler constraints were established while adding it. An `impl` must be
declared beside the implementing type (or at top level with a top-level
protocol); `impl Distant for Other::Type` from a third module does not
register the interface fact. Protocol parameters are constraints, not boxed
values, so heterogeneous collections of implementations are not expressible
through a protocol alone and no protocol value can be stored. The game stays
grid-based and does not use the shape protocol; proximity is an engine math
capability, not a forced refactor of gameplay.

## Upward composition predicates (2026-09-18)

`World::entity_is_actor` and `World::entity_is_enemy` make the set refinement
executable: `Enemy` is an `Actor`, so a `Guard` is both an enemy and an actor,
while a `Player` is only an actor. The cases are listed explicitly rather than
wildcarded, so adding a descendant forces the case-complete query to be updated
rather than silently matching. `test/world.elisa` pins the three relationships.
