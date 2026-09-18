# Capability status

Every capability is labelled so a claim can be checked instead of inferred:

- **Proved** — a proof under `proof/` establishes it, replayed in the gate.
- **Tested** — an executable test in `test/` exercises it and runs in
  `scripts/check.elisascript`.
- **Implemented** — code exists and a host runner or validation step exercises
  it, but it has no dedicated test.
- **Partial** — a named part of the plan is present, with the gap stated.
- **Planned** — not implemented; recorded so it is not implied.

The main gate is `~/.local/bin/elisascript scripts/check.elisascript`, which
writes `build/validation.json`. Host evidence comes from
`scripts/wicked_probe.elisascript` (native) and
`scripts/godot_capture.elisascript` (Godot), which need a display session.

## Identity, world, and runtime

| Capability | Label | Evidence |
|---|---|---|
| Monotonic `i64` identity, explicit exhaustion | Proved + Tested | `proof/entity_id.elisa`, `test/entity_id.elisa` |
| Owning `World`, liveness, storage, rollback, compaction | Proved + Tested | `proof/world.elisa`, `test/world.elisa` |
| Upward-composed hierarchy (`Enemy ⊆ Actor ⊆ Entity`) | Tested | `World::entity_is_actor/entity_is_enemy`, `test/world.elisa` |
| Compact live-entity column (O(live) iteration) | Tested | `World::world_live_column_valid`, `test/world.elisa` |
| Deterministic fixed-step headless game | Tested | `test/headless_game.elisa` |
| Host input path (device keys to portable names) | Tested | `src/runtime/input.elisa`, `native/input_probe.h`, `backends/godot/probe.gd` |
| Host embedding via C ABI (live input drives gameplay) | Tested + Implemented | `examples/maze/capi.elisa`, `native/embed_probe.cpp`, `scripts/embed_probe.py` |
| Declared read/write sets, derived execution waves | Tested | `src/runtime/schedule.elisa`, `src/runtime/executor.elisa`, `test/schedule.elisa` |
| Inspected counters, budgets, submitted bytes | Tested | `src/tooling/inspector.elisa`, `test/inspector.elisa` |

## Backends

| Capability | Label | Evidence |
|---|---|---|
| Canonical scene command stream, both hosts | Tested + Implemented | `src/backend/scene_bridge.elisa`, `test/scene_bridge.elisa`, `scripts/wicked_probe.elisascript`, `scripts/godot_capture.elisascript` |
| FFI ownership contracts, adversarial bridge | Tested | `src/backend/contracts.elisa`, `src/backend/fake_bridge.elisa`, `test/contracts.elisa`, `test/fake_bridge.elisa` |
| Scene resources rebuilt from canonical data | Tested | `src/backend/recording.elisa`, `test/recording.elisa` |
| Cross-host rendered comparison with tolerances | Implemented | `scripts/compare_renders.py`, both host runners |
| Native asset reload (release + rebuild) | Implemented | `native/reload_probe.h`, native runner |
| Teardown without accumulation (both hosts) | Tested + Implemented | `test/headless_game.elisa`, `native/churn_probe.h`, Godot unload audit |

## Game (maze vertical slice)

| Capability | Label | Evidence |
|---|---|---|
| Movement, collision, hazards, door/key, goal, lives | Tested | `examples/maze/game.elisa`, `test/maze_game.elisa` |
| Menu, pause, restart, quit, settings | Tested | `test/maze_game.elisa`, `examples/maze/main.elisa` |
| Won and lost paths, packaged entry point | Tested | `examples/maze/main.elisa`, `scripts/maze_game.elisascript` |
| Fog of war, status as portable data | Tested + Implemented | `test/maze.elisa`, host runners |
| Audio cues as data, playback on both hosts | Implemented | `native/audio_probe.h`, `backends/godot/capture.gd` |
| Play-in-editor (edit, undo/redo, validate, step) | Tested | `examples/maze/studio.elisa`, `test/maze_game.elisa` |
| Buffered input into gameplay (editor-feed queue) | Tested | `examples/maze/studio.elisa`, `test/maze_game.elisa` |
| Bounded chunk streaming driven by the player | Tested | `src/assets/streaming.elisa`, `test/maze_game.elisa` |
| Saved settings (versioned save blob) | Tested | `src/runtime/save.elisa`, `examples/maze/persist.elisa`, `test/maze_game.elisa` |

## Animation, navigation, physics, audio

| Capability | Label | Evidence |
|---|---|---|
| Animation state, clips, events, root motion | Tested | `src/animation/state.elisa`, `test/anim_state.elisa` |
| Keyframe clip sampling | Tested | `src/animation/sampler.elisa`, `test/anim_state.elisa` |
| Pose evaluation, blending, root motion, skinning payload | Tested | `src/animation/pose.elisa`, `test/anim_state.elisa` |
| Two-bone IK and aim constraints | Tested | `src/animation/ik.elisa`, `test/anim_state.elisa` |
| Linear blend skinning (bind-pose inverse) | Tested | `src/animation/skin.elisa`, `test/anim_state.elisa` |
| Character composes World + nav + animation + IK, unloads | Tested | `examples/maze/character.elisa`, `test/maze.elisa` |
| Root motion drives movement policy | Tested | `examples/maze/rootmotion.elisa`, `test/maze.elisa` |
| Character leg pose consumed by hosts | Tested + Implemented | `examples/maze/pose.elisa`, fixture `pose_*`, `backends/godot/probe.gd`, `native/pose_probe.h` |
| Grid navigation (BFS) | Tested | `src/nav/grid.elisa`, `test/grid_nav.elisa` |
| Physics authority (one solver per body) | Tested | `src/physics/policy.elisa`, `test/physics_policy.elisa` |
| Audio generation/ownership policy | Tested | `src/audio/policy.elisa`, `test/audio_policy.elisa` |
| Distance attenuation (spatial groundwork) | Tested | `AudioPolicy::attenuation`, `test/audio_policy.elisa` |

## Assets

| Capability | Label | Evidence |
|---|---|---|
| Descriptors, content IDs, bounds | Tested | `src/assets/descriptor.elisa`, `test/assets.elisa` |
| Versioned cooked package (v2 geometry) | Tested + Implemented | `scripts/cook_assets.py`, both hosts |
| Bounded import, crafted + fuzzed rejection | Tested | `scripts/cook_assets.py --self-test`, `asset_import_bounds` |
| Persistent SQLite catalogue | Implemented | `scripts/cook_assets.py`, `asset_catalogue_database` |
| zstd package round trip | Implemented | `native/zstd_probe.h` |
| Cooked RGBA texture consumed and rendered by both hosts | Tested + Implemented | texture package, `cooked_texture`, goal material on both hosts, `native/texture_upload.h` |
| Asset reload generations and stale rejection | Tested | `src/tooling/editor.elisa`, `test/editor.elisa` |

## Networking

| Capability | Label | Evidence |
|---|---|---|
| Fixed little-endian replication frame | Tested | `src/net/wire.elisa`, `test/session.elisa` |
| Loss/delay loopback link | Tested | `src/net/loopback.elisa`, `test/session.elisa` |
| Reordered delivery tolerance | Tested | `test/session.elisa` |
| Authority and revision ordering | Tested | `src/net/peer.elisa`, `test/session.elisa` |
| Client prediction and reconciliation | Tested | `src/net/prediction.elisa`, `test/replication.elisa` |
| Snapshot interpolation with clamping | Tested | `src/net/interpolation.elisa`, `test/session.elisa` |
| Recovery (gap detection, resync, high-water) | Tested | `src/net/recovery.elisa`, `test/session.elisa` |
| Reliable delivery window (retransmission) | Tested | `src/net/reliable.elisa`, `test/session.elisa` |
| Loss recovery end to end (window over the lossy link) | Tested | `test/session.elisa` |
| Determinism scope (solver, build) | Tested | `test/replication.elisa` |
| Fuzzed wire decoder | Tested | `test/session.elisa` |
| Integer-overflow rejection at the wire boundary | Tested | `Wire::encode` bounds, `test/session.elisa` |
| Real socket transport for the frame shape | Tested | `native/udp_probe.h` (UDP loopback) |
| GameNetworkingSockets transport | Planned | UDP loopback is a stand-in, not the selected library |

## Tooling, releases, and specialist libraries

| Capability | Label | Evidence |
|---|---|---|
| Pinned toolchains and provenance | Implemented | `scripts/record_validation.py`, `build/validation.json` |
| Reproducible release archive (mesh + texture + fixture) | Implemented | `scripts/package_release.py`, `release` in validation |
| 600-line source limit enforced | Implemented | `source_length_policy` |
| Code-reload quiescence and migration policy | Tested | `src/tooling/reload.elisa`, `test/editor.elisa` |
| Wicked rendering, Jolt physics | Implemented | `native/wicked_probe.cpp` |
| cgltf import, meshoptimizer cache optimization | Implemented | `native/asset_import.h`, `native/meshopt_probe.h` |
| ozz sampling, Recast/Detour navigation, miniaudio | Implemented | `native/ozz_probe.h`, `native/recast_probe.h`, `native/miniaudio_probe.h` |
| FreeType/HarfBuzz text | Implemented | `native/text_probe.h` |
| Tracy profiling client | Implemented | `native/tracy_probe.h` |
| Sanitizers at the untrusted boundary | Tested | `scripts/run_boundary_sanitized.py`, `native/boundary_harness.cpp` |
| Sanitized full graphics probe | Planned | sandbox aborts the instrumented graphics run |
| Live input driving the hosts | Planned | hosts replay Elisa-computed routes |
| Skinned-mesh submission to the hosts | Tested + Implemented | `examples/maze/pose.elisa`, fixture `skin_quad`, `backends/godot/probe.gd`, `native/skin_probe.h` |
| UI menu model (layout, focus, scrolling, hit test, activation) | Tested | `src/ui/menu.elisa`, `examples/maze/menu.elisa`, `test/maze_game.elisa` |
| UI box layout (orientation, padding, spacing) | Tested | `src/ui/layout.elisa`, `test/editor.elisa` |
| UI text line breaking (word packing) | Tested | `src/ui/text.elisa`, `test/editor.elisa` |
| Host UI consumes Elisa style data (row height) | Tested | fixture `menu_row_height`, `backends/godot/probe.gd` |
| Host UI consumes Elisa menu state | Tested | `backends/godot/probe.gd`, fixture `menu_*` fields |
| Native UI builds Wicked widgets from menu state | Implemented | `native/gui_probe.h` (verified, then removed before capture) |
| UI rendering toolkit | Partial | Godot controls, Wicked widgets, menu model, box layout, row-height style, and line breaking; no shaping or full typography |
| 16-bit packed texture (RGB565) | Tested | `scripts/cook_assets.py`, `cooked_texture_packed`, `backends/godot/probe.gd`, `native/texture_probe.h` |
| Block-compressed textures (KTX/Basis) | Planned | 16-bit packing only; no KTX/Basis toolchain |

## Verification

Re-run on the current tree:

- `elisascript scripts/check.elisascript` — exit 0, all checks pass, proofs
  replayed, `build/validation.json` written with the release and catalogue
  records.
- `python3 scripts/run_boundary_sanitized.py` — exit 0, no AddressSanitizer or
  UBSan finding over the boundary libraries.
- `elisascript scripts/wicked_probe.elisascript` — exit 0; frame verified,
  budget met; churn, ozz, Recast/Detour, miniaudio, text, zstd, reload, texture,
  UDP, menu, pose, and Wicked-GUI checks all pass.
- `elisascript scripts/godot_capture.elisascript` — exit 0; frames deterministic
  and the two hosts agree on scene semantics.
- `python3 scripts/embed_probe.py` — exit 0; the host links the Elisa C archive,
  drives gameplay, maps an SDL event to a move, and exercises the hazard rules.

## Deferred

These plan items are deliberately not implemented. The plan calls the library
roadmap "an integration order, not a command to add every dependency
immediately," and each of these is either optional for the first game or needs a
toolchain not present in this environment:

- **GameNetworkingSockets transport.** Replication, authority, prediction,
  interpolation, and recovery are implemented and tested, and a real UDP socket
  round trip exercises the byte boundary, but the selected transport library is
  not vendored or built here.
- **GPU texture compression (KTX/Basis).** The texture path is uncompressed
  RGBA applied to a material; no KTX/Basis toolchain is available.
- **Godot host embedding the game via the C ABI.** The native host drives
  gameplay through the Elisa C ABI; the Godot host still consumes the fixture
  and synthetic events, and wiring the C ABI into GDScript is follow-up work.
- **General UI layout/style system.** The menu model and host widgets exist, but
  there is no general layout, styling, or text-flow system.

Architectural decisions behind these boundaries are recorded in
[docs/adr/](adr/), in particular ADR-0011 on the canonical fixture as the
backend contract.
