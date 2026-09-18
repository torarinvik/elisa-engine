# ADR 0011: the canonical fixture is the backend boundary

## Status

Accepted.

## Context

The plan requires that both backend families consume the same Elisa-owned world
through backend-neutral commands, snapshots, assets, and results, and that
backend interchangeability mean "the same entity model, rules, semantic inputs,
and portable asset descriptions on both backends," not identical pixels or
solver trajectories. The engine's backends are separate programs (a Wicked-based
native host and a Godot host), so the boundary between Elisa and a backend is a
data channel, not an in-process call.

## Decision

`backends/scene_manifest.txt` is the single canonical channel. It is written by
the game and validated by `scripts/record_validation.py` before any host runs,
and it carries only portable data:

- **Topology and geometry**: `walls`, `mesh_asset`, `mesh_triangles`.
- **Game objects**: `goal`, `key`, `door`, `hazards`, `hunter`, `player_final`,
  `status_cell`, with positions in cell units.
- **Over-time gameplay**: `hunter_route` (the character's route), `game_status`.
- **Rules-derived presentation**: `fog_radius`, `audio_cues`.
- **UI**: `menu_actions`, `menu_enabled`, `menu_focus`.
- **Character pose**: `pose_hip`, `pose_knee`, `pose_foot` (engine-IK joints).
- **Input**: `input_forward`, `input_left`, `input_right` (portable button
  names, never enum ordinals).
- **Budgets**: `frame_budget_us`.

Hosts may not re-derive gameplay: they place markers at the given cells, walk
the given route, colour the status cell, hide geometry outside the fog radius,
build meshes from the cooked package, and map device events to the given button
names. Enum meanings are carried by name and by integer code that Elisa pins
(`game_status_code`), never by compiler-assigned ordinal.

Cooked assets are a second channel: `build/cooked/<stem>.pkg` (geometry),
`build/cooked/<stem>_tex.rgba` (texture), and the SQLite catalogue for tooling.
The runtime reads the packages, not source formats.

## Consequences

- A change to either host that re-derives rules, positions, or status instead of
  consuming the fixture fails the cross-host comparison and the fixture checks.
- The fixture is a stable, reviewable contract: `scripts/record_validation.py`
  gates its internal consistency, and `test/maze.elisa` pins the Elisa-side
  values it mirrors.
- Backend-specific features live behind explicit capabilities, not invisible
  branches; a host that cannot honour a field fails rather than silently
  ignoring it.
- Live input cannot drive Elisa gameplay across this boundary, because the
  channel is one-directional data. Closing that gap needs an embedding
  (Elisa-compiled gameplay callable from the host), which this ADR leaves
  explicitly out of scope.
