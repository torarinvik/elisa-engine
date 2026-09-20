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

`test/hierarchy.elisa` is part of the shared ElisaScript gate and covers parent
composition, cycle rejection, keep-world reparenting, parent removal,
interpolation, teleports, and physics-owned world publication.
