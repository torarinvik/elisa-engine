# Transform hierarchy validation

`src/world/hierarchy.elisa` owns a bounded local-to-world transform tree. It
rejects missing parents and cycles, propagates dirty transforms through deep
parent chains, supports keep-world reparenting, and detaches children to the
root when a parent is removed. Derived transforms use the shared Elisa TRS
composition and inverse helpers from `src/math/geometry.elisa`.

`test/hierarchy.elisa` is part of the shared ElisaScript gate and covers parent
composition, cycle rejection, keep-world reparenting, and parent removal.
Physics-owned transform arbitration and interpolation snapshots remain W02
follow-up work.
