# Physics body and shape validation

`src/physics/bodies.elisa` keeps shape and body identity in Elisa. Shapes can be
shared by static and dynamic bodies, while each body records its layer, mass,
kind, and motion authority (`Elisa` for static/kinematic or `Solver` for
dynamic). Generation-checked handles become the only native adapter boundary;
Jolt object IDs never enter gameplay data.

Destroying a shape is deferred while any live body references it. Destroying
the bodies then permits shape collection, and stale body handles are rejected.
`test/physics_bodies.elisa` covers shared shape ownership, authority selection,
identity counts, and unload order. Native Jolt shape construction and broadphase
layers remain P02 integration work.
