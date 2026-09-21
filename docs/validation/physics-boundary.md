# Jolt stepping boundary validation

The native probe creates a real Wicked rigid body and checks the Elisa-owned
simulation switch before measuring gravity. `native/physics_policy_probe.h`
disables simulation for two rendered frames and requires the body's position to
remain stable, then re-enables simulation; the existing settle phase confirms
that Jolt advances the body afterward.

The full SDL3/Metal native gate passed this pause/resume check on both
determinism runs. The generation-checked service also accepts a finite,
normalized target for a kinematic body, applies it through Wicked's transform
boundary, and rejects a dynamic body target; the application smoke covers both
paths against the real Jolt scene. The same service can explicitly request
deactivation or reactivation of a generation-checked dynamic body; a dedicated
sleep-state gate remains before this becomes a complete stability claim. The portable policy now also exposes an
Elisa-owned `StepClock`; `test/physics_policy.elisa` proves that a tick can be
active only once and commits must be contiguous. Sleeping, interpolation
interpolation equivalence across render rates, and competing-simulation
rejection remain P01 work.
