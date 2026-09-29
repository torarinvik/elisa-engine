# Buoyancy

Validated on 2026-09-29 with the freshly seeded Stage1 compiler. This is P11
progress (the floating-body half; soft bodies are not covered).

## Design

- `src/physics/buoyancy.elisa` (`PhysicsBuoyancy`) floats box-shaped bodies
  in integer units: grams, millinewtons, milliseconds, and micrometres for
  position and velocity. Integer millimetres left drag and motion in a dead
  zone below about 0.1 m/s, which kept a body bobbing forever; micrometres
  remove it.
- Force is Archimedes' `rho * g * V` over the submerged depth, and drag scales
  with the wetted fraction.
- Buoyancy has its own feature flag (`Disabled`) and a per-frame body budget
  (`OverBudget` holds the body still and reports it). A velocity change above
  2 m/s per step is clamped and reported as `Clamped`.

## Checks

- `test/physics_buoyancy.elisa` exits 0 (codes 1–8): a 500 kg, 1 m³ crate
  dropped from 2 m settles within 20 mm of its 0.5 m equilibrium draft with
  under 20 mm/s residual velocity after 30 s; a dense body sinks; the
  disabled flag and budget leave bodies untouched; a 1 s step is clamped.
- Negative control: removing drag makes the test exit 3 (no settling).

## Gaps

- Not connected to the Jolt physics service or rendering, no waves or
  rotation, and no soft bodies. The plan's representative scene is not built.
  P11 stays open. No proof harness.
