# Physics layer diagnostics

`src/physics/layer_diagnostics.elisa` (`PhysicsLayerDiagnostics`) mirrors the
32x32 collision-layer matrix given to the physics service. It sets layer pairs
in both directions, and it refuses layers outside 0..31 instead of clamping
them. `layer_report` returns four numbers:

- the enabled layer pairs;
- the isolated layers, which collide with nothing;
- the one-sided (asymmetric) pairs;
- a worst-case broadphase candidate count from bodies per layer: n*m pairs
  between two layers and n(n-1)/2 pairs inside one layer.

`layer_within_budget` fails on any asymmetry or on a count above the budget.

`test/physics_layer_diagnostics.elisa` (in the gate's unit-test list) uses
static, dynamic and debris layers with 10, 20 and 100 bodies. It expects 1390
candidates, then 6340 once debris collides with itself, which breaks a
2000-pair budget. It also checks that a one-sided cell is reported and does
not count as a collision.

Negative control: counting the pairs inside one layer as n*n makes the test
fail with code 8.

Limits: the matrix is a mirror kept by the engine, not read back from Jolt.
Wicked has no getter for the layer matrix. It is not yet wired to
`set_layer_collision`, and the counts come from the caller.
