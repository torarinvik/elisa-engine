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

## Play-in-editor (2026-09-18)

`examples/maze/studio.elisa` closes the authoring loop the plan describes: a
running game with an undo/redo history over its settings, stepped through the
public game API, plus asset-catalogue validation. Settings are packed into one
integer so the generic `Editor::History`, which records a single target/value
pair, can rewind them; a change that does not move the settings is not
recorded, and a change the game rejects is rolled back before it is recorded.
The studio's tile is usable only when the catalogue's current content hash and
generation match, so a recook makes the old package stale rather than silently
reusing it. `test/maze_game.elisa` pins the edit, undo, redo, step, and the
stale-generation rejection.
