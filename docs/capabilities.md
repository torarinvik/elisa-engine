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

**Forward work:** [the active implementation plan](../IMPLEMENTATION_PLAN.md)
prioritizes the native backend. Its unchecked tasks are future work; the evidence
labels here do not imply that a probe is a reusable, production-ready service.

## Identity, world, and runtime

| Capability | Label | Evidence |
|---|---|---|
| Monotonic `i64` identity, explicit exhaustion | Proved + Tested | `proof/entity_id.elisa`, `test/entity_id.elisa` |
| Owning `World`, liveness, typed storage access, rollback snapshots, compaction | Tested | `src/world/rollback.elisa`, `test/world.elisa`; provenance proof remains in `proof/world.elisa` |
| General typed world storage for unrelated game sets | Tested | `src/world/storage.elisa`, `test/world_storage.elisa`, `docs/validation/world-storage.md`; bounded actor/projectile columns share owned IDs and compact independently |
| Upward-composed hierarchy (`Enemy ⊆ Actor ⊆ Entity`) | Tested | `World::entity_is_actor/entity_is_enemy`, `test/world.elisa` |
| Compact live-entity column (O(live) iteration) | Tested | `World::world_live_reference_at`, `World::world_live_column_valid`, `test/world.elisa` |
| Persistent-to-runtime identity remapping with stale-reference rejection | Tested | `src/world/persistence.elisa`, `test/world.elisa` |
| Module-private fields for affine owners | Tested | `private:` sections in allocator, World, recorder, fake bridge, and lifetime queue; compiler rejection fixtures in `test/negative/*_private_*.elisa`, gated by `scripts/check.elisascript`; see [field privacy](field-privacy.md) |
| Deterministic fixed-step headless game | Tested | `test/headless_game.elisa` |
| Host input path (device keys to portable names) | Tested | `src/runtime/input.elisa`, `native/input_probe.h`, `backends/godot/probe.gd`; SDL3 is used by the engine, standalone host, and Wicked host |
| Shared Elisa-to-Wicked coordinate conversion for grid content | Tested + Implemented | `native/coordinate_conventions.h`, native wall/marker/live-input/route rendering |
| Bidirectional coordinate, transform-ABI, ray, asymmetric-scale, and winding-parity checks | Tested + Implemented | `native/coordinate_conventions.h`, `native/coordinate_abi.h`, `native/coordinate_fixture.h`, `native/coordinate_probe.h`, and native gate output `coordinates:`; the shared fixture is consumed by render/physics/skin/picking adapters |
| Versioned backend profiles with queried limits and feature requirements | Tested | `src/backend/capabilities.elisa`, `test/capabilities.elisa`; native feature population remains F08 work |
| Explicit backend requirement negotiation and fallback state | Tested | `Capabilities::negotiate_requirements`, `first_missing_requirement`, `fallback_requirement_count`, `test/capabilities.elisa`, and native optional-feature fallback gate |
| Native graphics capability, resource-format, worker, and memory query with explicit optional fallbacks | Tested + Implemented | ABI v2 in `native/capability_abi.h`, `native/capability_probe.h`, Wicked `GraphicsDevice::CheckCapability`/`CreateTexture`, queried high-priority and streaming worker counts, memory/viewport queries, and `ELISA_FORCE_OPTIONAL_FALLBACK=1` policy path in the native gate |
| Queried texture format selection and fallback policy | Tested | `native/texture_format_policy.h`, native capability gate; BC1/R16F/RGBA8 selection and normal-map BC1 rejection |
| Native logical handles and fence-delayed retirement | Tested | `native/resource_handles.h`, `native/resource_handle_probe.h`, `docs/validation/resource-handles.md` |
| SDL3 window lifecycle state and bounded fixed-step pacing | Tested + Implemented | `native/native_application.h`, `native/frame_pacer.h`, `native/window_lifecycle_probe.h`; focus/minimize/restore/resize transitions and capped catch-up run in the native gate |
| Host embedding via C ABI (drive + query + play through) | Tested + Implemented | `examples/maze/capi.elisa`, `native/embed_probe.cpp`, `scripts/embed_probe.py` |
| Persistent native host event loop (pause/resume/restart/close) | Tested | `native/live_game_probe.h`, `ELISA_PERSISTENT_HOST=1 ELISA_PERSISTENT_SELF_TEST=1`; the same SDL3 loop handles lifecycle events and real W/A/S/D input, while interactive display verification remains hardware-dependent |
| Versioned C ABI descriptor, session operations, feature negotiation, bounded spans | Tested + Implemented | `native/service_abi.h`, generated `maze_session_create/update/query/destroy`, malformed descriptor/table/buffer checks in `native/embed_probe.cpp` |
| Stable source/content/artifact asset descriptor with schema, variant, settings, and dependencies | Tested | `src/assets/descriptor.elisa`, `test/assets.elisa` |
| Transactional asset catalogue with dependency graph and deterministic cook cache | Tested + Implemented | `scripts/cook_assets.py`, `scripts/record_validation_assets.py`; WAL recovery, concurrent duplicate requests, diagnostics, dependencies, and cache reuse are covered by the cooker self-test and validation record |
| Bounded runtime package index and virtual-path validation | Tested + Implemented | `native/virtual_package.h`, `native/virtual_file_service.h`, `native/package_load.h`, `native/package_bounds_probe.h`; text packages and version-1 `ELPK` indexes validate, bounded zstd sections decompress, and coalesced reads honor cancellation and mount generations while traversal, duplicate/overlapping sections, malformed lines, unsupported compression, and missing files are rejected |
| C ABI bridge-call cost | Tested | `native/embed_probe.cpp` (100k exported query calls, `per_call_ns` printed) |
| Embedded game agrees with the canonical fixture | Tested | `native/embed_probe.cpp` (`embed fixture` check) |
| Godot host embeds Elisa gameplay through a hand-written GDExtension | Tested + Implemented | `backends/godot-embed/`, `scripts/godot_embed_probe.py` (full session matches the native embedding) |
| Declared read/write sets, derived execution waves | Tested | `src/runtime/schedule.elisa`, `src/runtime/executor.elisa`, `test/schedule.elisa` |
| Inspected counters, budgets, submitted bytes | Tested | `src/tooling/inspector.elisa`, `test/inspector.elisa` |
| Bounded editor inspector fields, selection, validation, and dirty edits | Tested | `src/tooling/inspector_view.elisa`, `test/editor.elisa` |
| Bounded editor asset browser with generation and stale-state tracking | Tested | `src/tooling/asset_browser.elisa`, `test/editor.elisa` |
| Bounded editor widget surface with buttons, toggles, sliders, and hit testing | Tested | `src/ui/widgets.elisa`, `test/inspector.elisa` |
| Bounded editor session composing panels, asset selection/loading, widgets, revisions, field undo/redo, and validated commit | Tested | `src/tooling/editor_session.elisa`, `test/editor_session.elisa` |
| Spawn/despawn churn with allocated bytes | Tested + Implemented | `native/churn_probe.h` (median/p95/worst plus steady-state heap delta, guard at 2 MiB), native runner |
| Generation/owner-checked handles with fence-delayed retirement queue | Tested + Implemented | `native/resource_handles.h`, `native/voice_handles.h`, `native/resource_handle_probe.h`, native Wicked probe; mesh/material/texture/body and miniaudio voice families invalidate logically before serial-gated collection |
| Elisa-owned Jolt simulation enable/pause boundary | Tested | `native/physics_policy_probe.h`, native Wicked gate; paused simulation holds a real dynamic body before gravity resumes, while fixed-step service ownership remains P01 work |
| Debug collision geometry (solid cells + markers, bounded boxes) | Tested + Implemented | `src/tooling/debug_geometry.elisa`, `examples/maze/debug.elisa`, `test/inspector.elisa`, `test/maze_game.elisa`, Godot wireframe overlay in `backends/godot/capture.gd` |

## Backends

| Capability | Label | Evidence |
|---|---|---|
| Canonical scene command stream, both hosts | Tested + Implemented | `src/backend/scene_bridge.elisa`, `test/scene_bridge.elisa`, `scripts/wicked_probe.elisascript`, `scripts/godot_capture.elisascript` |
| Persistent one-to-many render extraction | Tested | `src/backend/render_snapshot.elisa`, `test/render_snapshot.elisa`, `docs/validation/render-extraction.md`; native submission remains an adapter |
| Bounded transform hierarchy with checked reparenting | Tested | `src/world/hierarchy.elisa`, `test/hierarchy.elisa`, `docs/validation/transform-hierarchy.md`; physics arbitration remains W02 |
| Deferred structural world command buffer | Tested | `src/world/commands.elisa`, `test/world_commands.elisa`, `docs/validation/deferred-world-commands.md`; application to the primary registry remains W03 |
| FFI ownership contracts, adversarial bridge | Tested | `src/backend/contracts.elisa`, `src/backend/fake_bridge.elisa`, `test/contracts.elisa`, `test/fake_bridge.elisa` |
| Submission storage completion boundary | Tested | `src/runtime/lifetime.elisa`, `test/contracts.elisa` |
| Scene resources rebuilt from canonical data | Tested | `src/backend/recording.elisa`, `test/recording.elisa` |
| Cross-host rendered comparison with tolerances | Tested + Implemented | `scripts/compare_renders.py`, `scripts/wicked_probe.elisascript`, `scripts/godot_capture.elisascript` (deterministic captures agree on scene semantics) |
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
| Native dependency manifest and stale-artifact checks | Implemented | `native/dependency-manifest.json`, `scripts/check_dependency_manifest.py`, native driver preflight |
| Dependency provenance (pinned hashes/commits recorded) | Implemented | `dependency_provenance`, `dependencies` in validation |
| Reproducible release archive (mesh + textures + KTX + fixture + C ABI + Godot extension) | Implemented | `scripts/package_release.py`, `release` in validation |
| Automated platform testing | Partial | `.github/workflows/check.yml`: a Linux/macOS/Windows toolchain-free cook, KTX, source-length, and module-hygiene job plus a macOS job running the GNS, Basis, and ASan/UBSan boundary probes; the full gate still needs the pinned compiler/prover and a graphics session |
| 600-line source and documentation limit enforced | Implemented | `source_length_policy` covers engine sources, scripts, hosts, docs, and `Elisa_Engine_Architecture_and_Plan.md` |
| Production module namespace, owner-constructor, and named-constructor hygiene enforced | Implemented | `scripts/check_module_hygiene.py`, `module_hygiene_policy` in `build/validation.json` |
| Code-reload quiescence and migration policy | Tested | `src/tooling/reload.elisa`, `test/editor.elisa` |
| Wicked rendering, Jolt physics | Implemented | `native/wicked_probe.cpp` |
| cgltf import, meshoptimizer cache optimization | Implemented | `native/asset_import.h`, `native/meshopt_probe.h` |
| ozz sampling, Recast/Detour navigation, miniaudio | Partial + Implemented | `native/ozz_probe.h`, reusable `native/navmesh_service.h` exercised by `native/recast_probe.h`, bounded `native/miniaudio_service.h` exercised by `native/miniaudio_probe.h`; navigation ownership/query and clip/voice ownership adapters are integrated, while cooked multi-tile navigation, device recovery, streaming, spatial audio, and Elisa agent movement remain N01–N03/S01–S03 |
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
| Portable and native asynchronous asset-loader contract | Tested | `src/assets/loader.elisa`, `test/asset_loader.elisa`, `native/native_resource_loader.h`; request coalescing, staged decode/upload, cancellation, placeholders, generations, bounded native texture upload, and telemetry are covered; worker scheduling and production decode remain A04 |

## Verification

Re-run on the current tree:

- `elisascript scripts/check.elisascript` — exit 0: all 32 runtime suites pass
  and the proof step is green again. `proof/entity_id.elisa` is 17/17
  obligations replayed and `proof/world.elisa` 6/6, status `proved` with no
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

## Roadmap gaps carried into the native plan

These are unimplemented or partial beyond the evidence above. The active
[implementation plan](../IMPLEMENTATION_PLAN.md) now gives them concrete tasks
and consumers; their earlier maze-only deferral is not the current priority policy.

- **Box2D:** P10 adds a genuine 2D service and playable example.
- **ACL:** C07 adds and benchmarks an optional compression/decoder path.
- **Steam Audio:** S04 adds opt-in acoustics after the native audio service.
- **ufbx:** A09 adds FBX import through the normalized asset pipeline.
- **Richer editor authoring:** E01–E10 build persistent native authoring workflows
  over the existing bounded models, including scene, prefab, asset, and subsystem editing.
- **Arbitrary code hot reload:** E10 first provides safe rebuild/restart, then
  permits reload only after quiescence, callback draining, and migration are verified.
- **Effekseer:** X07 remains a specialist expansion with a concrete authoring
  requirement; Wicked effects are the first native path in R09.

The plan also distinguishes the existing specialist-library probes from the
reusable runtime APIs, authoring, packaging, and scale evidence still required.

Architectural decisions behind these boundaries are recorded in
[docs/adr/](adr/): ADR-0011 on the canonical fixture as the backend contract,
ADR-0012 on the host C ABI, and ADR-0013 on the hand-written Godot GDExtension.
