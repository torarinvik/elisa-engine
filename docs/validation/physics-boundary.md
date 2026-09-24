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
deactivation or reactivation of a generation-checked dynamic body. The native
body gate also publishes a real kinematic target through Wicked's scene-transform
owner path, rejects dynamic targets, holds a dynamic pose while sleeping for a
tick, and verifies gravity resumes after wake. The portable policy now also
exposes an Elisa-owned `StepClock`; `test/physics_policy.elisa` proves that a
tick can be active only once and commits must be contiguous. The integrated
SDL3/Metal cadence capture runs one real Jolt body for one simulated second at
30 and 120 presentation frames. It checks equal committed ticks and poses,
verifies that render pumps do not step the body, and compares the final Wicked
backbuffer captures. See [`physics-render-cadence.md`](physics-render-cadence.md).
Intermediate-frame equivalence and the full clock-to-hierarchy path remain P04
work.
