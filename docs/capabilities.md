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
| Deterministic fixed-step headless game | Tested | `test/headless_game.elisa` |
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
| Authority and revision ordering | Tested | `src/net/peer.elisa`, `test/session.elisa` |
| Client prediction and reconciliation | Tested | `src/net/prediction.elisa`, `test/replication.elisa` |
| Snapshot interpolation with clamping | Tested | `src/net/interpolation.elisa`, `test/session.elisa` |
| Recovery (gap detection, resync, high-water) | Tested | `src/net/recovery.elisa`, `test/session.elisa` |
| Determinism scope (solver, build) | Tested | `test/replication.elisa` |
| Fuzzed wire decoder | Tested | `test/session.elisa` |
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
| Skinned-mesh submission to the hosts | Planned | joint positions consumed; no skinning upload |
| UI menu model (layout, focus, hit test, activation) | Tested | `src/ui/menu.elisa`, `examples/maze/menu.elisa`, `test/maze_game.elisa` |
| Host UI consumes Elisa menu state | Tested | `backends/godot/probe.gd`, fixture `menu_*` fields |
| Native UI builds Wicked widgets from menu state | Implemented | `native/gui_probe.h` (verified, then removed before capture) |
| UI rendering toolkit | Partial | Godot controls and Wicked widgets from Elisa data; no general layout/style system |
| Compressed textures (KTX/Basis) | Planned | uncompressed RGBA texture applied to a material; no GPU compression |

## Verification

Re-run on the current tree:

- `elisascript scripts/check.elisascript` — exit 0, 32 checks, proofs replayed,
  `build/validation.json` written with the release and catalogue records.
- `elisascript scripts/wicked_probe.elisascript` — exit 0; frame verified,
  budget met, churn/ozz/Recast/miniaudio/text/zstd/reload/udp/menu all pass.
- `elisascript scripts/godot_capture.elisascript` — exit 0; frames deterministic
  and the two hosts agree on scene semantics.
- `python3 scripts/run_boundary_sanitized.py` — clean AddressSanitizer/UBSan run
  over the boundary libraries.
