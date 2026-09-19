# Jolt stepping boundary validation

The native probe creates a real Wicked rigid body and checks the Elisa-owned
simulation switch before measuring gravity. `native/physics_policy_probe.h`
disables simulation for two rendered frames and requires the body's position to
remain stable, then re-enables simulation; the existing settle phase confirms
that Jolt advances the body afterward.

The full SDL3/Metal native gate passed this pause/resume check on both
determinism runs. This proves the ownership boundary and pause behavior, not
the complete P01 fixed-step service: explicit engine tick scheduling,
interpolation, body-handle lifecycle, and a competing-simulation rejection are
still open tasks.
