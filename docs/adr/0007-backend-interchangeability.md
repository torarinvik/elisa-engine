# ADR 0007: Backend interchangeability scope

**Status:** accepted (2026-09-18)

## Context

"Runs on both backends" is meaningless without stating what is portable
and what is allowed to differ. Byte-identical pixels and identical
physics trajectories across different solvers are not achievable goals.

## Decision

- Portable: entity model, rules, semantic inputs, portable asset
  descriptions, scene lifecycle semantics. Backend-specific: shader
  languages, pixel values within a declared tolerance, solver
  trajectories under different integrators.
- Graphics comparison uses an explicit tolerance (per-channel peak plus
  mean bound, e.g. 0.02/0.005) instead of byte equality.
- Strict replay or multiplayer equivalence additionally requires the
  same solver and build, recorded as a `Determinism{solver, build}`
  scope; anything else is out of scope by declaration, not by accident.
- The maze bundle manifest binds all of the above per shippable: assets,
  scene identity, required capabilities, determinism scope, frame budget.

## Evidence

`src/backend/capabilities.elisa`, `src/backend/image_compare.elisa`,
`src/net/replication.elisa` (scope), `examples/maze/bundle.elisa`,
`backends/scene_manifest.txt`, both host probes.

## Not covered

Automated screenshot capture and comparison runs; Godot capability
profiles beyond headless; custom-shader equivalence policy.
