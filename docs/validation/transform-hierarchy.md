# Transform hierarchy validation

`src/world/hierarchy.elisa` owns a bounded local-to-world transform tree. It
rejects missing parents and cycles, propagates dirty transforms through deep
parent chains, supports keep-world reparenting, and detaches children to the
root when a parent is removed. Derived transforms use the shared Elisa TRS
composition and inverse helpers from `src/math/geometry.elisa`.

Each published world pose carries a previous snapshot for render sampling.
Local gameplay writes use `hierarchy_set_local`, while a physics solver uses
`hierarchy_apply_physics` to publish the solver's world pose and derive the
next local pose. Teleports copy the new pose into both snapshots, so they do
not smear across a render frame. The stateless `PhysicsInterpolation::sample_pair`
operation keeps interpolation state private to its owner module.

`World::World` now owns a 256-node hierarchy alongside its checked registry.
Spawning and despawning keep the two stores in lockstep; `world_set_transform`
publishes an authoritative world pose, `world_reparent` validates branded
parent references (including an explicit root reference), and rollback captures
and validates hierarchy state with the gameplay columns. The inspector fixture
qualifies `Inspector::Snapshot` so the public hierarchy snapshot type does not
leak across module namespaces.

`test/hierarchy.elisa` and `test/world.elisa` are part of the shared
ElisaScript gate and cover parent composition, cycle rejection, keep-world
reparenting, parent removal, interpolation, teleports, physics-owned world
publication, a 31-node deep-chain stress case, and primary-world hierarchy
integration. `test/hierarchy_benchmark.elisa` plus
`scripts/hierarchy_benchmark.py` measure a full 256-node chain over 16 rounds,
with eight root-dirty propagation passes per round. On macOS 27.0 / Apple M5,
nine fresh-process runs measured a 582.427 ms median and 607.918 ms p95:

```text
hierarchy benchmark: runs=9 median_ms=582.427 p95_ms=607.918 min_ms=571.051 max_ms=607.918 binary_bytes=117232
```
