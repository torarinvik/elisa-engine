# ADR 0002: Elisa owns the world; backends are services

**Status:** accepted (2026-09-17)

## Context

Gameplay, entity lifecycle, scheduling, and asset orchestration must not
be split across two renderers, and native code must stay small and
auditable.

## Decision

- Elisa owns entity existence, identity, gameplay state, authored
  configuration, and change sequencing. Backends own GPU resources and
  derived scene representations, recreatable from Elisa state.
- One canonical scene command stream (`SceneRecorder`) feeds every host;
  `src/backend/scene_bridge.elisa` validates epoch, identity, lifecycle
  order, and viewport before either probe consumes it.
- Physics is a delegated simulation: Elisa selects exactly one solver per
  body (`src/physics/policy.elisa`), supplies inputs, steps once, and
  commits poses/events at a defined tick boundary. Kinematic motion comes
  from Elisa, dynamic motion from the solver result, never both.
- Backend selection happens at startup against explicit capability
  profiles; unsupported configurations fail before gameplay starts.

## Evidence

`src/backend/recording.elisa`, `src/backend/scene_bridge.elisa`,
`src/backend/capabilities.elisa`, `src/physics/policy.elisa`,
`backends/scene_manifest.txt`, `backends/godot/probe.gd`,
`native/wicked_probe.cpp`, `examples/maze/bundle.elisa`.

## Not covered

Packaged per-target binaries; full GDExtension backend; Wicked game loop
beyond the one-frame smoke; native solver linkage (Jolt/Godot physics).
