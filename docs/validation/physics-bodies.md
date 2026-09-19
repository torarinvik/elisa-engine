# Physics body and shape validation

`src/physics/bodies.elisa` keeps shape and body identity in Elisa. Shapes can be
shared by static and dynamic bodies, while each body records its layer, mass,
kind, and motion authority (`Elisa` for static/kinematic or `Solver` for
dynamic). Generation-checked handles become the only native adapter boundary;
Jolt object IDs never enter gameplay data.

Destroying a shape is deferred while any live body references it. Destroying
the bodies then permits shape collection, and stale body handles are rejected.
`test/physics_bodies.elisa` covers shared shape ownership, authority selection,
identity counts, and unload order. `native/physics_body_bridge.h` now maps
typed static, kinematic, and dynamic requests to Wicked rigid-body components,
keeps owner/generation-checked handles, rejects foreign or stale handles, and
returns native body counts to baseline in the native gate. Compound shape
cooking and broadphase layer adapters remain.
