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
| Host input path (device keys to portable names) | Tested | `src/runtime/input.elisa`, `native/input_probe.h`, `backends/godot/probe.gd`; SDL3 is used by the engine, standalone host, and Wicked host |
| Host embedding via C ABI (drive + query + play through) | Tested + Implemented | `examples/maze/capi.elisa`, `native/embed_probe.cpp`, `scripts/embed_probe.py` |
| C ABI bridge-call cost | Tested | `native/embed_probe.cpp` (100k exported query calls, `per_call_ns` printed) |
| Embedded game agrees with the canonical fixture | Tested | `native/embed_probe.cpp` (`embed fixture` check) |
| Godot host embeds Elisa gameplay through a hand-written GDExtension | Tested + Implemented | `backends/godot-embed/`, `scripts/godot_embed_probe.py` (full session matches the native embedding) |
| Declared read/write sets, derived execution waves | Tested | `src/runtime/schedule.elisa`, `src/runtime/executor.elisa`, `test/schedule.elisa` |
| Inspected counters, budgets, submitted bytes | Tested | `src/tooling/inspector.elisa`, `test/inspector.elisa` |
| Bounded editor inspector fields, selection, validation, and dirty edits | Tested | `src/tooling/inspector_view.elisa`, `test/editor.elisa` |
| Bounded editor asset browser with generation and stale-state tracking | Tested | `src/tooling/asset_browser.elisa`, `test/editor.elisa` |
| Bounded editor widget surface with buttons, toggles, sliders, and hit testing | Tested | `src/ui/widgets.elisa`, `test/inspector.elisa` |
| Bounded editor session composing panels, asset state, widgets, and revisions | Tested | `src/tooling/editor_session.elisa`, `test/editor_session.elisa` |
| Spawn/despawn churn with allocated bytes | Tested + Implemented | `native/churn_probe.h` (median/p95/worst plus steady-state heap delta, guard at 2 MiB), native runner |
| Debug collision geometry (solid cells + markers, bounded boxes) | Tested + Implemented | `src/tooling/debug_geometry.elisa`, `examples/maze/debug.elisa`, `test/inspector.elisa`, `test/maze_game.elisa`, Godot wireframe overlay in `backends/godot/capture.gd` |

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
| Behavior abstraction via protocols (point/box/ray proximity) | Tested | `Geometry::Distant`, `within_radius`, `nearest_distance`, `test/geometry.elisa` |
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
| Catalogue browsing and validation | Tested | `src/assets/database.elisa`, `test/catalogue.elisa` |

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
| Netcode convergence fuzz across loss/delay profiles | Tested | `test/session.elisa` |
| Integer-overflow rejection at the wire boundary | Tested | `Wire::encode` bounds, `test/session.elisa` |
| Real socket transport for the frame shape | Tested | `native/udp_probe.h` (UDP loopback) |
| GameNetworkingSockets transport | Tested + Implemented | `scripts/fetch_gns.py` (pinned a424b7db), `native/gns_probe.cpp`, `scripts/gns_probe.py` (33-byte replication frame over a real loopback connection) |

## Tooling, releases, and specialist libraries

| Capability | Label | Evidence |
|---|---|---|
| Pinned toolchains and provenance | Implemented | `scripts/record_validation.py`, `build/validation.json` |
| Dependency provenance (pinned hashes/commits recorded) | Implemented | `dependency_provenance`, `dependencies` in validation |
| Reproducible release archive (mesh + textures + KTX + fixture + C ABI + Godot extension) | Implemented | `scripts/package_release.py`, `release` in validation |
| Automated platform testing | Partial | `.github/workflows/check.yml`: a Linux/macOS cook + KTX job and a macOS job running the GNS, Basis, and ASan/UBSan boundary probes; the full gate still needs the pinned compiler/prover and a graphics session |
| 600-line source limit enforced | Implemented | `source_length_policy` |
| Code-reload quiescence and migration policy | Tested | `src/tooling/reload.elisa`, `test/editor.elisa` |
| Wicked rendering, Jolt physics | Implemented | `native/wicked_probe.cpp` |
| cgltf import, meshoptimizer cache optimization | Implemented | `native/asset_import.h`, `native/meshopt_probe.h` |
| ozz sampling, Recast/Detour navigation, miniaudio | Implemented | `native/ozz_probe.h`, `native/recast_probe.h`, `native/miniaudio_probe.h` |
| FreeType/HarfBuzz text | Implemented | `native/text_probe.h` |
| Tracy profiling client | Implemented | `native/tracy_probe.h` |
| Sanitizers at the untrusted boundary | Tested | `scripts/run_boundary_sanitized.py`, `native/boundary_harness.cpp` |
| UBSan full graphics probe | Tested | `ELISA_SANITIZER=undefined CXX=scripts/cxx_sanitize.py elisascript scripts/wicked_probe.elisascript`; found and fixed signed-shift UB in `native/package_load.h` |
| ASan full graphics probe | Tested | `ELISA_SANITIZER=address,undefined CXX=scripts/cxx_sanitize.py elisascript scripts/wicked_probe.elisascript`; the SDL3-native Wicked build passes scene creation, churn, rendering, live input, deterministic capture, and the frame budget under ASan+UBSan |
| Live input driving the embedding hosts | Tested + Implemented | `native/embed_probe.cpp` (SDL3 event → move code) and `backends/godot-embed/godot_embed_probe.gd` (synthetic key → move code → `maze_step`) |
| Live input driving the native rendered host | Tested + Implemented | `native/live_game_probe.h`, `scripts/wicked_probe.elisascript` (live frame non-blank after an SDL key drives the game) |
| Live input driving the Godot rendered capture | Tested + Implemented | `scripts/build_godot_extension.py`, `backends/godot/capture.gd` (synthetic key → `maze_step` → live marker → verified live frame) |
| Skinned-mesh submission to the hosts | Tested + Implemented | `examples/maze/pose.elisa`, fixture `skin_quad`, `backends/godot/probe.gd`, `native/skin_probe.h` |
| UI menu model (layout, focus, scrolling, hit test, activation) | Tested | `src/ui/menu.elisa`, `examples/maze/menu.elisa`, `test/maze_game.elisa` |
| UI box layout (orientation, padding, spacing) | Tested | `src/ui/layout.elisa`, `test/editor.elisa` |
| UI text line breaking (word packing) | Tested | `src/ui/text.elisa`, `test/editor.elisa` |
| Host UI consumes Elisa style data (row height) | Tested | fixture `menu_row_height`, `backends/godot/probe.gd` |
| Host UI consumes Elisa menu state | Tested | `backends/godot/probe.gd`, fixture `menu_*` fields |
| Native UI builds Wicked widgets from menu state | Implemented | `native/gui_probe.h` (verified, then removed before capture) |
| Engine UI style (surface/label colors, insets) applied by both hosts | Tested + Implemented | `src/ui/style.elisa`, `test/editor.elisa`, fixture `menu_style_*`, `backends/godot/probe.gd`, `native/gui_probe.h` |
| UI rendering toolkit | Implemented | Godot controls, Wicked widgets, menu model, box layout, theme style, and line breaking; shaping stays host-provided (FreeType/HarfBuzz natively, Godot's own text) rather than an engine typography model |
| 16-bit packed texture (RGB565) | Tested | `scripts/cook_assets.py`, `cooked_texture_packed`, `backends/godot/probe.gd`, `native/texture_probe.h` |
| Block-compressed texture (BC1/DXT1) | Tested | `scripts/cook_assets.py`, `cooked_texture_bc1`, `backends/godot/probe.gd`, `native/texture_probe.h` |
| KTX container for cooked textures, consumed by both hosts | Tested | `scripts/cook_assets.py` (`maze_tile_tex.ktx`), `backends/godot/probe.gd` (`load_ktx_from_buffer`, gated), `native/texture_probe.h` (`probe_texture_ktx`) |
| KTX2/Basis supercompression | Tested + Implemented | `scripts/fetch_basisu.py` (pinned 99f52d63), cooker writes `maze_tile_tex.ktx2`, `native/basisu_probe.cpp` transcodes to RGBA, Godot loads it compressed |

## Verification

Re-run on the current tree:

- `elisascript scripts/check.elisascript` — exit 0: all 32 runtime suites pass
  and the proof step is green again. `proof/entity_id.elisa` is 15/15
  obligations replayed and `proof/world.elisa` 2/2, status `proved` with no
  replay gaps. The branch-fact regression is fixed in `elisa-proof` 12c79ab
  (see ADR-0010); the engine source was never changed for it.
- `python3 scripts/run_boundary_sanitized.py` — exit 0, no AddressSanitizer or
  UBSan finding over the boundary libraries.
- `ELISA_SANITIZER=undefined CXX="$PWD/scripts/cxx_sanitize.py" elisascript
  scripts/wicked_probe.elisascript` — exit 0, the full graphics probe under
  UBSan with no report.
- `ELISA_SANITIZER=address,undefined CXX="$PWD/scripts/cxx_sanitize.py" elisascript
  scripts/wicked_probe.elisascript` — exit 0, the full SDL3-native graphics
  probe under ASan+UBSan with no report.
- `elisascript scripts/wicked_probe.elisascript` — exit 0; frame verified,
  budget met; churn, ozz, Recast/Detour, miniaudio, text, zstd, reload, texture
  (including the KTX container), UDP, menu, pose, and Wicked-GUI checks all pass.
- `elisascript scripts/godot_capture.elisascript` — exit 0; frames deterministic,
  the two hosts agree on scene semantics, and the live-input frame rendered from
  the embedded game is non-blank.
- `python3 scripts/embed_probe.py` — exit 0; the host links the Elisa C archive,
  drives gameplay, maps an SDL3 event to a move, and exercises the hazard rules.
- `python3 scripts/godot_embed_probe.py` — exit 0; the Godot host dumps its own
  GDExtension header, builds the bridge over the same archive, plays a full
  session that matches the native embedding move for move, and drives gameplay
  from live input through the extension.
- `python3 scripts/gns_probe.py` — exit 0; GameNetworkingSockets is fetched,
  built, linked, and moves the engine's 33-byte replication frame over a real
  loopback connection unchanged.
- `python3 scripts/basisu_probe.py` — exit 0; the pinned Basis encoder cooks
  `maze_tile_tex.ktx2` and the Basis transcoder decodes it to the cooked green.
  The gated Godot scene probe loads the same file as a compressed image; run
  `python3 scripts/fetch_basisu.py` once so the cooker can emit it.

## Plan-gated (not implemented by design)

The plan's library roadmap is "an integration order, not a command to add every
dependency immediately," and each of these is gated on a representative need:

- **Box2D** waits for a genuine 2D use case; the first game is 3D.
- **ACL** is a later benchmarked alternative codec path; `src/animation/codec.elisa`
  records the codec choice rather than shipping a second decoder.
- **Steam Audio** is optional spatial acoustics; `src/audio/policy.elisa` keeps
  spatial voices behind an explicit opt-in for when it is added.
- **Effekseer** needs a concrete authoring need; Wicked supplies effects for now.
- **ufbx** is a fallback importer; the glTF path covers the shipped assets.
- **Arbitrary code hot reload** stays out of scope until quiescence, callback
  draining, and state migration are demonstrated; the bounded policy and asset
  reload path are implemented and tested (`src/tooling/reload.elisa`).

## Deferred

These plan items are deliberately not implemented. The plan calls the library
roadmap "an integration order, not a command to add every dependency
immediately," and each of these is either optional for the first game or needs a
toolchain not present in this environment:

- **Richer editor authoring.** The bounded session, inspector, asset browser,
  and widget surface are implemented; undo/redo integration and richer
  content workflows remain deferred until the editor has a concrete asset
  editing path.

Architectural decisions behind these boundaries are recorded in
[docs/adr/](adr/): ADR-0011 on the canonical fixture as the backend contract,
ADR-0012 on the host C ABI, and ADR-0013 on the hand-written Godot GDExtension.
