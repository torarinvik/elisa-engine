# Physics interpolation validation

`src/physics/interpolation.elisa` separates fixed-step publication from render
sampling. Simulation commits monotonically increasing ticks and publishes the
previous/current transforms; rendering samples a bounded alpha without changing
the tick. Teleports bypass blending for one sample, preventing a long smear
across an origin reset or respawn.

`test/physics_interpolation.elisa` covers fractional samples, duplicate-tick
rejection, teleports, and pose validity. `src/world/hierarchy.elisa` stores the
latest authoritative world pose alongside the previous committed pose, and
`src/runtime/world_rendering.elisa` exposes `extract_hierarchy`, which samples
that history for presentation without mutating `World`. `test/hierarchy.elisa`
and `test/world_rendering.elisa` verify parent-aware publication and a midpoint
render sample while the authoritative transform remains at the latest tick.
`native/physics_interpolation_probe.h` mirrors the bounded publication contract
at the Wicked boundary and the native gate verifies fractional interpolation,
duplicate rejection, and teleport bypass. Native Jolt pose wiring, sleeping,
and render-rate equivalence remain.
