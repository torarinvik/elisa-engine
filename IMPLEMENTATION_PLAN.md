# Elisa Engine — native implementation backlog

**Updated:** 2026-09-20. **Focus:** reusable Wicked + SDL3 backend services and their library integrations.
**Baseline:** engine `867ccd5`; inspect the current tree before starting work.
This is the active execution plan. [Architecture](Elisa_Engine_Architecture_and_Plan.md)
defines the ownership model; [capabilities](docs/capabilities.md) records evidence.
Unfinished tasks below are proposals, not claims of existing engine support.

## Product objective and priorities

Build a reusable, authorable, shippable native game engine beyond the maze demo.
Deliver a persistent playable host, reusable subsystem services, a practical editor,
and packaged games using the same Elisa-owned world and public API.

- **P0:** remove foundations that currently prevent a reusable native engine.
- **P1:** deliver the common features needed to build and ship a substantial 3D game.
- **P2:** add depth, scale, multiplayer, production tooling, and broader platforms.
- **P3:** specialist capabilities, with a real example and a measured need.
- Near-term work prioritizes making the existing Wicked, SDL3, Jolt, miniaudio,
  cgltf, BasisU, meshoptimizer, ozz, Recast/Detour, and GameNetworkingSockets
  integrations into usable engine services. Prefer finishing these vertical slices
  over adding another dependency or a probe-only wrapper.
- Godot receives compatibility maintenance for changed shared contracts. New features
  may initially be native-only; report unsupported capabilities explicitly. Godot
  installation or renderer feature parity must not block the native development gate.
- Reuse the selected stack. Do not introduce another renderer, foreign gameplay ECS,
  high-level UI framework, or competing public scheduler to avoid engine work.
- Box2D, ACL, Steam Audio, FBX import, and richer authoring now have concrete tasks.
  They are no longer dismissed because the maze demo did not need them. Follow their
  prerequisites; do not fetch the entire specialist-library roadmap in advance.

## What exists, and what must not be mistaken for completion

The checked world, private owner fields, expression style, affine rejection fixtures,
maze gameplay, scene recorder, asset cooking, and implementation-linked proofs exist.
Preserve them. The current runtime executor is a **serial reference**, not a thread pool.
`native/wicked_probe.cpp` is a finite diagnostic host: it uses a canonical manifest,
synthetic input, and library probes. It is not a general game runner.
Jolt currently appears through Wicked's physics path; ozz, Recast/Detour, miniaudio,
Tracy, text, and GNS have integration evidence of varying depth, often probe-only.
A linked library, a policy enum, or one successful scene is not a public runtime service.
The existing source inventory is the starting point, not a reason to rewrite working code.

## Agent execution contract

1. Pick the first highest-priority task with completed prerequisites. Work through one
   coherent playable or testable slice; do not scaffold every wrapper before using one.
2. Read the relevant existing modules, ADRs, local library headers, and pinned source.
   Verify APIs and actual platform support there; this plan does not certify upstream features.
3. Elisa owns gameplay, world identity, scheduling policy, asset orchestration, animation
   behavior, navigation decisions, replication, editor models, and the public API.
   Keep C/C++/Objective-C shims narrow: opaque handles, plain ABI data, vendor operations.
4. Use last-expression returns, scoped loops with result bindings, and block expressions
   that contain temporary variables. Use `def Type() -> Type` constructors and named constants.
   Prefer suitable Elisa standard-library facilities over new engine utilities.
5. Use qualified module dependencies, small public surfaces, private owner fields, and
   phase-limited borrows. Keep vendor types out of public Elisa interfaces. Stay below
   600 lines per source/document file, including this plan; split by responsibility.
6. Preserve real error unions and failure atomicity. Define resource/thread/allocator
   ownership, callback lifetime, cancellation, capacity behavior, and destruction order.
7. A compiler, ElisaScript, or prover limitation blocking this style becomes a minimized
   regression and a fix in its owning repository. Do not flatten scopes or weaken checks
   as the permanent workaround; record the required toolchain revision.
8. Completion requires a public API used by real gameplay/editor code, positive and
   adversarial tests, native evidence where applicable, and documentation. Mocks validate
   contracts but cannot establish library or GPU integration. Tests must assert outcomes.
9. Record performance claims in optimized builds: warm-up policy, hardware, scene size,
   CPU/GPU time, p50/p95/p99, allocations, and peak memory. Qualifiers and block syntax
   should add no runtime machinery; verify generated code when that is the claim.
10. Change `[ ]` to `[x]` only with commit, command, result, artifact path, and limitations
    recorded in a linked `docs/validation/` note. Update capability labels precisely.
    Commit coherent changes. Do not mark a subsystem complete from its first smoke test.
11. If blocked, record the exact cause and a concrete prerequisite task, then continue
    another ready task. Hardware-unavailable checks remain unverified, never silently green.
    Product decisions such as the project's license do not block unrelated implementation.

## Delivery milestones

| Milestone | Exit evidence | Main task groups |
|---|---|---|
| M0 — reusable host | Visible interactive maze, proper close, resize, restart, reproducible native-only command; existing probes still pass | F01–F10, R01, I01 |
| M1 — reusable game runtime | Authored scene with multiple assets, streamed resources, PBR, collisions, save/load; no maze-specific host logic | W01–W06, A01–A06, R02–R05, R13, R15, P01–P04, S01–S02 |
| M2 — character playground | Animated controllable character, terrain/slopes, moving platforms, nav agents, spatial sound, debug overlays | C01–C08, N01–N04, P05–P08, S03–S04, R06–R09 |
| M3 — author and package | Editor creates/edits/saves a second game; standalone Release package runs outside the source tree | E01–E09, I02–I07, Q01–Q04, Q07 |
| M4 — scale and network | Measured crowd/streaming scene and two-process multiplayer game; soak tests and native CI evidence | W07–W10, N05–N06, T01–T08, Q05–Q09 |
| M5 — breadth | Tested opt-in advanced systems, each exercised by a shipped example | Remaining P2/P3 tasks |

**Next foundation work: F05, F08, and F10**, then complete R01 so an Elisa world
drives the persistent Wicked scene instead of the diagnostic manifest. Continue with
the P1 rendering, physics, asset, audio, and input services in dependency order.
Do not restart completed identity or field-privacy work. Milestones are outcome gates;
individual feature tasks may advance as soon as their explicit dependencies are ready.

## F — native foundation and lifecycle

- [x] **F01 · P0 · Evidence and integration inventory** — After: none.
  Inspect `native/`, `src/backend/`, fetchers, CI, and the pinned Wicked fork; classify each service as policy, probe, reusable adapter, or game-integrated. Done: one inventory names entry points, owners, exact build identities, gaps, and commands; reconcile outdated completion claims without erasing historical evidence. Evidence: [`docs/native-integration-inventory.md`](docs/native-integration-inventory.md), committed as `f9cc344`; `wc -l docs/native-integration-inventory.md` (88) and `python3 scripts/check_source_length.py` passed. This inventory records the current finite-probe limitations; it does not claim F02–F10 completion.
- [x] **F02 · P0 · Persistent native application** — After: F01.
  Extract reusable startup/frame/shutdown code from `native/wicked_probe.cpp`; keep the probe as a client. Done: a visible maze runs until user close, pauses/resumes, restarts a world, and accepts real input without hardcoded frame counts or a manifest defining gameplay authority. Evidence: [`docs/validation/persistent-host.md`](docs/validation/persistent-host.md); `ELISA_PERSISTENT_HOST=1 ELISA_PERSISTENT_SELF_TEST=1 build/wicked-native-probe ... alwaysactive` passed with pause/resume, restart, movement, fixed ticks, close, and ordered shutdown. The self-test is deterministic; manual visual input remains a host-session check.
- [x] **F03 · P0 · Reproducible native dependency build** — After: F01.
  Lock Wicked, local patches, SDL3, Jolt, and currently used libraries/toolchains in a machine-readable manifest with checksums and build options. Done: a clean checkout can build using configured paths; stale artifacts are detected, and active targets neither fetch nor link obsolete SDL2. Evidence: `native/dependency-manifest.json` and `scripts/check_dependency_manifest.py` committed as `a6a7f9d`; `python3 scripts/check_dependency_manifest.py` passed against Wicked `21a1262b` after the SDL3-safe audio shutdown patch; the native driver preflight and full two-pass `elisascript scripts/wicked_probe.elisascript` passed using SDL3 and the recorded external artifacts. The manifest intentionally reports machine-specific paths through environment overrides; it does not vendor third-party source.
- [x] **F04 · P0 · Versioned service ABI** — After: F02, F03.
  Define narrow C entry points and Elisa bindings for create/update/query/destroy, bounded spans, errors, allocator ownership, callback thread affinity, and feature/version negotiation. Done: ABI size/alignment tests and a real host client reject incompatible versions and malformed descriptors without exposing vendor structs.
  Evidence: [`docs/validation/service-abi.md`](docs/validation/service-abi.md), `python3 scripts/embed_probe.py` passed the generated Elisa archive through `ElisaServiceV1`, including create/update/query/destroy, version/size/allocator/buffer/stale-handle rejection, and ABI static assertions; committed with the implementation. F02 remains a separate persistent-host gate.
- [ ] **F05 · P0 · Orderly startup and shutdown** — After: F04.
  Ensure the normal runner returns through ordered world teardown, callback drain, audio stop, GPU completion, and worker/device/window cleanup; patch pinned Wicked lifetime gaps. Done: partial-initialization failure and repeated scene restart leak no resources; normal process exit succeeds without forced termination.
  Progress: `NativeApplication` rolls back SDL/Wicked initialization failures at SDL, window, and Wicked stages. Shutdown closes callback admission, runs reverse-order hooks, drains Wicked jobs, then stops audio while SDL is live, waits for GPU work, destroys the Wicked application, clears the global device, destroys the window, and quits SDL. The shutdown-hook test submits a job and verifies it completes before device teardown. A fixed 64-entry function/context registry checks overflow rejection and reverse teardown order. Wicked commit `e45123b` destroys placement-constructed command lists on Metal, Vulkan, and DX12 and adds `WaitForAllJobs()`. Evidence: [`docs/validation/orderly-shutdown.md`](docs/validation/orderly-shutdown.md); the two-pass native gate passed. F05 remains incomplete: eight repeated host/device cycles retain about 1.93 MiB linearly, and macOS leak reports still include FAudio, Metal shader `dispatch_data`, and system allocations. Attribute and resolve persistent growth before marking the no-leak exit criterion complete.
- [x] **F06 · P0 · Native resource handles and retirement** — After: F04.
  Generalize generation and submission-lifetime contracts to real mesh, material, texture, body, and voice handles; separate logical destruction from fence-delayed release. Done: stale and cross-world handles, generation exhaustion, failed creation, bounded-capacity rejection, and destroy-while-in-flight are exercised against real resources. Evidence: [`docs/validation/native-resource-retirement.md`](docs/validation/native-resource-retirement.md); the two-pass Wicked native gate passed pool saturation, queue saturation/recovery, voice cleanup on generation exhaustion, deferred texture/voice release, stale-handle rejection, and object-baseline checks. Production imported-mesh upload and material binding remain tracked under A05, R02, and R04.
- [x] **F07 · P0 · Coordinate and numeric conventions** — After: F04.
  Centralize units, handedness, matrix order, quaternion layout, depth, winding, tangent parity, and camera conversion instead of probe-local sign flips. Done: round trips and an asymmetric scene validate render, physics, skinning, ray picking, and negative/nonuniform scale conventions. `native/coordinate_abi.h` v2 validates/converts column-major affine matrices, normalized XYZW quaternions, and signed tangent-frame parity; `native/coordinate_transform_bridge.h` submits TRS/matrix payloads to Wicked with explicit units and row-vector conversion, rejecting shear. Elisa and native tests apply the same basis-reflection and negative-scale rule. Wicked commit `a149914` stores world-transform orientation in existing `ShaderMeshInstance` padding and applies it to raster and ray-tracing tangent frames; the normal-mapped asymmetric signed-scale render compares exact rerun pixels against [`backends/coordinate_reference.png`](backends/coordinate_reference.png). Picking verifies a center hit and a miss beyond a negatively/nonuniformly scaled cube. A dynamic signed-scale box is stepped and queried against Jolt; Wicked commit `bf8945b` uses scale magnitudes for primitive collision dimensions. The skin fixture uploads Elisa-deformed vertices, applies a signed nonuniform transform, uses shared winding parity, and checks all four resulting Wicked world positions. Evidence: [`docs/validation/coordinate-conventions.md`](docs/validation/coordinate-conventions.md); `scripts/check.elisascript` and the two-pass `scripts/wicked_probe.elisascript` passed.
- [ ] **F08 · P0 · Runtime capability negotiation** — After: F04.
  Replace coarse profile assumptions with queried service/format/limit/feature support and explicit fallback policy. Done: unavailable renderer features and missing optional libraries yield actionable errors or tested fallbacks; a dependency's advertised feature cannot automatically mark the engine capability supported. Progress: Elisa resolves bounded feature and limit requirements into `Ready`, `Fallback`, `Unavailable`, or `Invalid`; `negotiate_limit` includes remaining memory, and typed texture negotiation matches the native BC1/normal-map/alpha fallback rules. `negotiate_requirements` now needs an explicit per-feature fallback capability map and returns `Unavailable` when a missing service has no declared handler, even if callers globally allow fallback. `test/capabilities.elisa` exercises declared, missing, and disallowed fallback behavior independently for all eight service features. ABI v2 in `native/capability_abi.h` carries optional graphics bits, queried RGBA8/BC1/R16F support, memory budget/usage, and actual Wicked high-priority/streaming worker counts. `maze_backend_configure` maps the validated SDL3/Wicked host report to the live Elisa profile before game startup; removing Input rejects start, and malformed reconfiguration preserves the last valid profile. Unknown resource capacities remain zero and cannot satisfy requirements. Typed C queries validate individual features, formats, and viewport/worker/memory limits; unknown bits and inconsistent profiles are rejected. Evidence: [`docs/validation/capability-negotiation.md`](docs/validation/capability-negotiation.md), the full Elisa suite, and the two-pass native gate. Wider runtime fallback handling for every service remains.
- [x] **F09 · P0 · SDL3 lifecycle and frame pacing** — After: F02, F07.
  Handle focus, high-DPI resize, minimize, fullscreen, display changes, close, and suspended simulation; separate fixed ticks from presentation. Done: zero-sized windows and variable render cadence do not corrupt simulation, busy-spin, stretch viewports, or lose input transitions. `NativeApplication::WindowState` suspends on focus loss, minimize, or zero pixel extent; `run_frame()` skips presentation while suspended, and `advance_fixed()` clears backlog. Backing size and DPI come from Wicked's native window properties to preserve Cocoa Retina drawable dimensions; window metric changes recreate the existing Metal swapchain and refresh the canvas before presentation. `native/input_tick_queue.h` keeps movement and restart events in bounded FIFO order until fixed ticks consume them; overflow is explicit. Evidence: [`docs/validation/sdl3-lifecycle.md`](docs/validation/sdl3-lifecycle.md); the two-pass native gate passed synthetic display-change, actual SDL resize/fullscreen/backbuffer checks, and fixed-tick input tests; the persistent-host self-test passed pause/resume, restart, movement, and close.
- [ ] **F10 · P0 · Native-first validation command** — After: F03, F05, F08, F09.
  Add composable ElisaScript quick/headless/native-GPU checks over existing gates, with structured evidence and provenance. Done: the native gate runs without Godot installed, invalidates stale success, distinguishes skip from pass, and retains shared-contract and optional Godot regression commands. Progress: `scripts/native_gate.elisascript` provides quick/headless/native modes, clears stale reports, uses explicit pass/fail/skip stage semantics, and keeps Godot out of the native prerequisites. All repository checks resolve from the gate's script path, independent of the caller's working directory. Native mode builds the Wicked host, captures each rendered frame, and verifies artifacts through bounded nested processes to stay within ElisaScript's process-capture time limit. `scripts/write_native_gate_report.py` writes schema-2 provenance (checkout revision, host, Python/SDK, timestamp) and distinguishes hardware-verified native runs from policy-only or headless runs; F05 completion and broader CI bootstrap remain.

## W — world, scenes, persistence, and execution

- [ ] **W01 · P1 · General world storage beyond the maze** — After: F04.
  Extend the checked world with registered engine/game types and measured dynamic storage, preserving algebraic modeling and branded identity. Done: two unrelated game entity sets coexist; growth failure, compaction, and destruction preserve invariants without maze-specific actor limits or a replacement foreign ECS.
  Progress: `src/world/storage.elisa` adds an affine typed catalogue with separate actor/projectile columns, one owned monotonic ID stream, typed lookup, destruction, and independent compaction; `test/world_storage.elisa` now fills the 128-row actor column, rejects growth beyond capacity, destroys a middle row, and verifies compaction plus monotonic replacement IDs in `scripts/check.elisascript`. Integration with the primary `World` registry and timed storage measurements remain.
- [ ] **W02 · P1 · Transform hierarchy** — After: W01, F07.
  Add local/world transforms, parenting, dirty propagation, interpolation snapshots, and checked reparenting policy. Done: cycle rejection, keep-world/keep-local behavior, deep trees, parent deletion, and physics-owned transforms agree with rendered positions.
  Progress: `src/world/hierarchy.elisa` provides bounded local/world TRS nodes, dirty propagation, cycle and missing-parent rejection, keep-world reparenting with full TRS inversion, root detachment on parent removal, interpolation snapshots, teleport bypass, and an explicit physics-owned world-pose publication path; `test/hierarchy.elisa` covers all of those behaviors plus a 31-node deep-chain propagation stress case in the shared gate. Primary-world integration and measured large-tree timing remain.
- [ ] **W03 · P1 · Deferred structural mutation** — After: W01.
  Implement phase-bound spawn/despawn/reparent command buffers with stable ordering and an explicit atomicity policy. Done: iteration borrows cannot outlive the phase; conflicting commands, allocation failure, and despawn during events cause neither partial entities nor dangling borrows.
  Progress: `src/world/commands.elisa` provides a bounded affine spawn/despawn/reparent buffer with insertion order, atomic duplicate-write rejection, and capacity-aware commit that preserves the batch on allocation failure; `test/world_commands.elisa` covers those contracts in the shared gate. Applying accepted commands to `World` and borrow lifetime diagnostics remain.
- [ ] **W04 · P1 · Scene and prefab instances** — After: W02, W03, A01.
  Define versioned scene/prefab data, stable authoring IDs, runtime remapping, nested instances, and overrides. Done: the same prefab can spawn twice without identity collisions; save/reload preserves overrides and rejects cycles or missing references with useful diagnostics.
  Progress: `src/world/prefab.elisa` defines versioned bounded nodes, stable authoring IDs, per-instance runtime remapping, transform overrides, and affine instance validation; `test/prefab.elisa` covers two instances, missing parents, cycles, and override isolation. Nested instances, serialized overrides, and primary-world integration remain.
- [ ] **W05 · P1 · Streaming world cells** — After: W04, A04, F06.
  Extend bounded maze streaming into asynchronous scene-cell activation with dependencies, hysteresis, cancellation, and unload policy. Done: a player crosses cell boundaries while referenced resources remain valid; repeated travel stays within declared CPU/GPU memory budgets.
  Progress: `src/world/cell_streaming.elisa` adds bounded generation-checked cell requests, dependency-generation validation, resident budgets, cancellation, and hysteretic trimming; `test/cell_streaming.elisa` covers coalescing, stale dependencies, budget rejection, unload, and reactivation. Native decode/upload scheduling and primary-world cell activation remain.
- [ ] **W06 · P1 · Savegame schema and migrations** — After: W04.
  Serialize stable data and IDs, excluding native handles; add transactional writes, checksums, migration chains, and crash recovery. Done: old-version, truncated, oversized, and corrupt saves are tested; loading replaces the world atomically and reconstructs native state.
  Progress: `src/world/save_schema.elisa` adds stable-ID records, deterministic checksums, a bounded version-1-to-2 migration, and affine staged commit/abort; `test/save_schema.elisa` covers commit, migration, checksum validation, and abort isolation. `scripts/save_journal.py` now provides fsynced canonical payloads, journaled atomic replacement, interrupted-write recovery, and corrupt-journal fallback in the shared gate. Truncated-save diagnostics and atomic replacement of the complete runtime world remain.
- [ ] **W07 · P2 · Real parallel executor** — After: W03, F05.
  Keep `src/runtime/executor.elisa` as the serial oracle; execute proven-independent work on a reusable worker pool using Elisa concurrency facilities where suitable. Done: cancellation, shutdown, dependencies, and effect/resource conflicts are tested; parallel results match the serial reference for deterministic systems.
  Progress: `native/parallel_executor.h` now executes bounded approved waves concurrently with a custom barrier, joins each wave before the next, caps workers, and propagates task exceptions; the Wicked gate proves overlap, wave ordering, cap rejection, and failure handling. Elisa schedule-to-native dispatch and serial trace equivalence remain.
- [ ] **W08 · P2 · World events and service phases** — After: W03, F04.
  Define bounded typed event queues and an explicit input/simulation/physics/animation/render/audio order, including overflow and unsubscribe behavior. Done: reentrant callbacks, listener destruction, and worker-to-main delivery cannot mutate a world during an invalid access phase.
  Progress: `src/world/events.elisa` adds bounded typed events, explicit six-phase ordering, listener subscriptions, unsubscription, phase-gated emission, and single-delivery drains; `test/world_events.elisa` covers phase regression and listener removal. `native/world_event_bridge.h` now adds a mutex-protected worker-to-main handoff with fixed storage, phase guards, one-shot delivery, and unsubscribe suppression; world-borrow integration and reentrant callback tests remain.
- [ ] **W09 · P2 · Replay and state diagnostics** — After: W06, W08.
  Record tick-stamped input, random seeds, decisions, and state digests with scoped determinism guarantees. Done: replay finds the first divergent subsystem/tick; floating-point and solver/build boundaries are declared instead of promising cross-platform bit identity.
  Progress: `src/runtime/replay.elisa` records bounded tick/input/seed/world/physics/render frames, declares same-build versus cross-build scope, and reports the first divergent tick; `test/replay.elisa` covers matching, divergence, and duplicate ticks. Persistent trace I/O, input serialization, and digest production remain.
- [ ] **W10 · P2 · Storage scale and world proofs** — After: W03, W07.
  Benchmark hot iteration, spawn/despawn, lookup, and compaction; improve measured layouts and grow implementation-linked proofs where supported. Done: representative small/large worlds show before/after timings and memory, and proof claims name exact invariants rather than claiming whole-engine safety.

## A — assets, cooking, loading, and content pipelines

- [x] **A01 · P0 · Stable asset identity and schema** — After: F01.
  Extend descriptors with distinct source identity, content hash, artifact variant, schema version, dependencies, and import settings. Done: renames preserve references; changing content/settings/tool versions invalidates exactly the affected artifacts; IDs are not process hashes or filesystem paths. Evidence: `Assets::AssetDescriptor` and `test/assets.elisa` in commit `f17ace0`; `../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/assets-test test/assets.elisa && build/assets-test` passed, including path-rename, content-change, schema, variant, and malformed-identity cases.
- [x] **A02 · P1 · Incremental asset database** — After: A01.
  Extend the existing SQLite catalogue into a transactional dependency graph, import diagnostics, and deterministic cook cache; select the content-hash implementation from the chosen stack. Done: interrupted cooks recover, concurrent requests deduplicate, and reproducible cache hits survive process restart.
  Evidence: [`docs/validation/asset-catalogue.md`](docs/validation/asset-catalogue.md), `scripts/cook_assets.py --self-test` passed (`7 crafted + 96 fuzzed documents rejected` plus rollback/concurrency/cache checks), real `python3 scripts/cook_assets.py "$PWD"` produced schema-2 catalogue rows, and `record_validation_assets.py` accepted the dependency/cache invariants; committed with the implementation.
- [ ] **A03 · P1 · Runtime package and virtual filesystem** — After: A01, F04.
  Evolve current cooked packages into bounded indexed bundles with zstd, alignment, versioning, overrides, and async-friendly reads. Done: runtime loading works outside the checkout; traversal, overlapping sections, decompression bombs, and missing dependencies are rejected before allocation or upload. Progress: `native/virtual_package.h` parses bounded version-1 `ELPK` indexes, decompresses bounded zstd sections, and resolves safe logical names through explicit override roots before the shipped base root with generation metadata; `native/virtual_file_service.h` adds bounded coalesced requests, cancellation, pump budgets, stale mount-generation rejection, dependency-generation tokens that invalidate queued reads after a remount, dependency existence/order validation, and a worker future for bounded asynchronous pumps. The native package gate covers worker scheduling and dependency rejection; dependency graph ordering across package manifests remains.
- [ ] **A04 · P1 · Asynchronous resource loader** — After: A02, A03, F06.
  Implement request/coalesce/cancel/prioritize, worker decode, device-thread upload, residency, and budgeted eviction. Done: unloading during read/decode/upload and failed dependencies leave no leaked handle; frame threads do not block on file IO and placeholder behavior is explicit.
  Progress: `src/assets/loader.elisa` and `test/asset_loader.elisa` define bounded generation-checked request coalescing, priority updates, explicit queued/decoding/uploading/resident stages, cancellation, placeholders, and byte-budget eviction. `native/native_resource_loader.h` now connects those stages to `VirtualFileService` and the Wicked device phase: the native gate proves coalesced compressed reads, bounded decode, real texture upload, cancellation, stale dependency rejection, upload telemetry, and a worker-future IO phase separated from caller-thread upload. Production decoders and full in-flight cancellation remain.
- [ ] **A05 · P1 · Complete glTF scene import** — After: A02, F07.
  Extend cgltf import to scene hierarchies, mesh primitives, indices, materials, cameras, lights where supported, skins, morphs, and animation through normalized engine assets. Done: a representative authored scene renders correctly; unsupported extensions fail visibly rather than silently losing content.
  Progress: `src/assets/scene.elisa` defines a bounded normalized scene contract for stable nodes, primitives, cameras, lights, skins, morph metadata, parent-cycle checks, and unsupported-extension rejection; `test/asset_scene.elisa` covers the contract. `native/asset_import.h` now traverses cgltf nodes/primitives/cameras/lights/skins/animations/morph targets, validates PBR factors/alpha metadata and texture references, recognizes supported Basis texture extensions, rejects unsupported or malformed content, and decodes a bounded first primitive into finite engine arrays. The Wicked gate uploads that authored primitive through a real mesh component and renders it as the goal marker while proving a pinned material fixture; multiple primitive/subset material binding and richer authored scenes remain.
- [ ] **A06 · P1 · Production texture pipeline** — After: A03, F08.
  Extend KTX2/Basis handling to mip chains, color spaces, normal maps, alpha policy, cubemaps, and supported GPU-native transcodes. Done: runtime selects queried formats, avoids unnecessary RGBA expansion, and validates malformed containers and texture memory budgets.
  Progress: `native/texture_format_policy.h` selects RGBA8, BC1, or R16F from queried capability bits, rejects BC1 for authored alpha, and forces normal maps to an RGBA8-safe path even when a scalar format was requested; `native/texture_upload.h` validates and uploads bounded cooked RGBA KTX1 payloads, while `native/ktx2_upload.h` uses the pinned Basis transcoder to upload every bounded 2D KTX2 mip with a 64 MiB decoded budget and preserves its linear/sRGB transfer function in Wicked's texture format. The Wicked gate feeds the KTX2 resource to the authored goal material. Cubemap upload and GPU-native transcode selection remain.
  Progress: `src/backend/material.elisa` and `test/material.elisa` define validated PBR texture slots, scalar factors, alpha policy, and normal-map identity at the engine boundary; native KTX2 mip/color-space policy remains.
- [ ] **A07 · P1 · Mesh optimization and LOD cooking** — After: A05.
  Use meshoptimizer for vertex/index optimization, simplification, compression, and optional meshlet data only where the selected render path consumes it. Done: screen-error LOD selection preserves boundaries and material subsets; visual errors, bytes, cook time, and render cost are measured.
  Progress: `src/assets/lod.elisa` defines a bounded deterministic LOD chain with screen-error selection, material-subset preservation, duplicate/capacity checks, and invalid-order rejection; `test/asset_lod.elisa` is part of the shared gate. `native/meshopt_probe.h` now performs bounded meshoptimizer simplification on the authored primitive, validates index reduction/error, and reports the cooked artifact measurement before the native mesh upload; multi-LOD package emission, subset-specific simplification, and render-path LOD selection remain.
- [ ] **A08 · P1 · Tangents and authored lightmap UVs** — After: A05.
  Integrate MikkTSpace and xatlas as offline stages with deterministic settings, seam handling, and metadata. Done: mirrored UV normal mapping and a UV-overlap fixture validate output; tools are not pulled into the game runtime unnecessarily.
- [ ] **A09 · P2 · FBX import with ufbx** — After: A05, C01.
  Implement FBX-to-normalized mesh/skeleton/animation/material conversion with documented axis/unit and unsupported-feature policy. Done: the same animated character imported from glTF and FBX has comparable bind pose, playback, and material assignment; malformed input is bounded.
- [ ] **A10 · P1 · Safe asset hot reload** — After: A04, W04.
  Stage replacement dependency graphs and swap generations at a safe frame boundary, retaining old GPU/audio resources until consumers finish. Done: edited textures/materials/meshes update live; a broken replacement preserves the last good scene and reports import errors.
- [ ] **A11 · P2 · Collision and navigation cook artifacts** — After: A05, P02, N01.
  Cache Jolt collision data and Recast tiles by geometry/settings/version; add CoACD convex decomposition for selected dynamic concave assets. Done: runtime never repeats offline decomposition; hull error/cost limits and incompatible artifact rebuilds are tested.
- [ ] **A12 · P2 · Asset import fuzzing and workers** — After: A05, A06.
  Expand bounded/fuzzed corpus coverage across model, image, animation, and package import; isolate expensive untrusted imports in cancellable worker processes. Done: corrupt inputs, timeouts, and crashes produce reproducible diagnostics without corrupting the catalogue or editor session.

## R — Wicked rendering as an engine service

- [ ] **R01 · P0 · Render extraction and entity mapping** — After: F04, F06, F07.
  Replace maze/manifest-specific rendering with an Elisa-owned frame snapshot or batched change stream and explicit one-to-many render IDs. Done: spawn/update/despawn reaches a persistent scene; vendor-only entities never become gameplay identities, and render updates cannot advance gameplay independently.
  Progress: `src/backend/render_snapshot.elisa` adds a bounded persistent snapshot keyed by world-branded gameplay references and stable render IDs; one gameplay entity can fan out to multiple instances, updates preserve the row, and removal compacts it. `test/render_snapshot.elisa` and `native/render_snapshot_bridge.h` are gated; the native bridge creates, updates, and removes independent Wicked rows while preserving the gameplay/render identity boundary. Its bounded transactional batch path validates all rows before committing, rejects duplicate or non-finite data, and rolls back newly created entities on staging failure. Full scene submission still needs to replace maze-specific manifest construction.
- [ ] **R02 · P1 · Mesh/material/instance API** — After: R01, A04.
  Expose reusable static/skinned mesh instances, material slots, visibility, layers, and shared resources with batched updates. Done: hundreds of instances share one mesh/material; modifying or destroying an instance cannot corrupt another, and ABI traffic is measured.
  Progress: `src/backend/render_resources.elisa` provides generation-checked instance handles, shared mesh/material asset IDs, transform/visibility/layer updates, reference counting, and an atomic bounded instance-update batch; `test/render_resources.elisa` verifies independent updates, duplicate-batch rejection, and stale-handle rejection. `native/resource_handles.h` now applies material color, visibility, and layer-mask updates through owner/generation-checked Wicked handles, and records batch call/row/rejection telemetry as an ABI traffic measure; the native gate covers two independent rows and duplicate rejection. Shared mesh upload, material slots, and large-instance throughput remain.
- [ ] **R03 · P1 · Cameras and viewports** — After: R01, F09, W02.
  Implement perspective/orthographic cameras, camera switching, aspect policy, render-to-texture, picking rays, and editor/game views. Done: resize, split view, offscreen targets, frustum boundaries, and asymmetric projections behave correctly at high DPI.
  Progress: `src/backend/camera.elisa` provides validated perspective/orthographic state, high-DPI viewport resizing, split-viewport pixel-to-ray conversion, TRS-aware picking rays, and clip-plane policy; `test/camera.elisa` covers both projections, resize/suspend, and valid/invalid split rectangles. `native/camera_bridge.h` creates/removes real Wicked perspective and orthographic views, applies high-DPI dimensions, resizes projections, and switches a live `RenderPath3D` between cameras in the native gate. Render targets, frustum culling, and scheduling multiple paths/viewports remain.
- [ ] **R04 · P1 · PBR material workflow** — After: R02, A05, A06.
  Expose Wicked-backed base color, metallic/roughness, normals, emissive, transparency, double-sidedness, and supported material features via engine descriptors. Done: a material reference scene and authored imports validate texture channels, tangent conventions, and color handling.
  Progress: `native/pbr_material_bridge.h` now validates and applies base/emissive color, metalness, roughness, opaque/mask/blend alpha policy, double-sidedness, shadow flags, and a bounded base-color texture slot through generation-checked Wicked material handles; the native gate covers foreign-handle rejection, unload, and a cooked Basis/KTX2 resource reaching the authored goal material. Authored glTF material upload and tangent/color reference captures remain.
  Progress: `src/backend/material.elisa` provides the backend-neutral PBR descriptor and alpha/double-sided policy; `test/material.elisa` covers valid and invalid channel/factor combinations. Wicked material upload, authored reference captures, and tangent/color validation remain.
- [ ] **R05 · P1 · Lights, shadows, and environment** — After: R03, R04.
  Add directional/point/spot lights, sky/environment maps, shadow settings, exposure, and feature-dependent lighting controls. Done: moving lights, shadow bias, transparent objects, and indoor/outdoor scenes have stable reference captures and explicit quality/cost settings.
  Progress: `src/backend/lighting.elisa` defines validated backend-neutral directional/point/spot descriptors (including transforms) and sun/ambient/sky/fog values. `native/lighting_bridge.h` owns generation-checked Wicked lights and realtime environment probes, applies validated weather settings, moves light transforms, normalizes directions, and tests invalid sun direction plus unload baselines. Authored sky assets, exposure/profile integration, shadow-bias policy, and material/light reference captures remain.
- [ ] **R06 · P1 · Debug drawing and picking** — After: R03, W02.
  Provide scoped lines, shapes, text, depth-tested overlays, entity picking, and selection outlines from Elisa data. Done: render/physics/nav bounds can be compared in one view, picking returns checked world references, and disabled debug drawing has no steady-state allocations. Progress: `src/tooling/debug_geometry.elisa` and `test/inspector.elisa` define bounded gameplay collision geometry; `native/debug_draw_bridge.h` validates and flushes bounded boxes, lines, and copied depth-tested text, `native/picking_bridge.h` maps real Wicked ray hits to generation-checked gameplay references, and `native/selection_outline_bridge.h` toggles Wicked material outlines with explicit clear. End-to-end editor picking overlays remain.
- [ ] **R07 · P1 · Post-processing and quality profiles** — After: R05, F08.
  Expose supported antialiasing/upscaling, tonemapping, bloom, fog, depth effects, and render scale through validated profiles. Done: transitions resize history buffers safely; unsupported options report fallback, and profiles carry measured GPU/VRAM costs.
  Progress: `src/backend/quality.elisa` defines validated low/medium/high profiles with explicit tonemap, upscaler, bloom, AO, SSR, fog, depth, and render-scale policy; `native/postprocess_bridge.h` applies those settings to Wicked, controls scene height fog, restores probe weather, and reports unsupported FSR as a fallback. History-resource transition validation and measured GPU/VRAM costs remain.
- [ ] **R08 · P1 · Animation and morph submission** — After: R02, C02.
  Submit skeleton palettes and morph weights with explicit ownership and buffering to Wicked's supported deformation paths. Done: multiple independent animated instances share source assets, bounds update correctly, and in-flight pose memory survives until device consumption.
  Progress: `native/animation_submission_bridge.h` now owns generation-checked, double-buffered bone palettes and morph weights, applies them to Wicked armature/mesh components, rejects non-finite or out-of-range data, and keeps a submitted buffer live until explicit completion. Multiple independent authored instances, bounds verification, and Elisa-driven runtime submission remain.
- [ ] **R09 · P1 · Native particle and decal services** — After: R02, W08.
  Expose Wicked particle/decal authoring and event-driven spawning with pooling, owner attachment, time scaling, and cleanup. Done: a combat or environmental scene uses effects through Elisa; restart and owner despawn return counts and memory to baseline.
  Progress: `native/effect_bridge.h` now provides bounded generation-checked emitter/decal handles, validates count/lifetime/color/range policy, updates real Wicked particle and decal components, ticks emitter simulation, and restores component counts on destruction. Elisa event spawning, owner attachment, time scaling, and a rendered combat scene remain.
- [ ] **R10 · P2 · Visibility, batching, and LOD** — After: R02, A07.
  Connect spatial bounds, culling, instance batches, distance/screen-error LOD, and supported occlusion paths. Done: a measured large scene improves frame cost without missing newly visible objects, breaking animated bounds, or relying on warmed probe-only query state.
  Progress: `native/visibility_lod_bridge.h` now applies bounded draw-distance, LOD-bias, layer, renderable, and occlusion-culling policy to generation-checked Wicked objects; the native gate verifies update, foreign/stale rejection, and cleanup. Elisa screen-error selection and measured large-scene batching remain.
- [ ] **R11 · P2 · Terrain and vegetation rendering** — After: W05, A07, R10.
  Add chunked heightfield terrain, material layers, collision/nav alignment, and instanced vegetation with quality limits. Done: a traversable streamed landscape has stable seams, bounded residency, consistent picking, and measured overdraw.
- [ ] **R12 · P2 · Advanced lighting paths** — After: R05, R07, F08.
  Expose selected Wicked GI/reflection/ray-tracing features only after checking the pinned backend and device support. Done: a representative lighting scene validates enabled and fallback paths with quality/performance evidence; missing hardware remains explicitly unverified.
- [ ] **R13 · P1 · Shader pipeline and warm-up** — After: F03, R04.
  Package versioned shaders, permutations, compilation diagnostics, and pipeline-cache keys; support offline preparation where the backend permits it. Done: clean-machine startup uses packaged inputs, cache invalidation is correct, and cold/warm frame hitches are measured separately.
- [ ] **R14 · P2 · Device failure and rendering recovery** — After: F05, F06, R13.
  Define device/swapchain failure reporting and recover or exit cleanly using authoritative asset/world state. Done: injected upload/resize/device failures release resources safely; recovery, where supported, reconstructs the scene without duplicating gameplay entities.
- [ ] **R15 · P1 · Render graph and transient targets** — After: F06, R03, R13.
  Let Elisa define ordered render passes, resource reads/writes, transient targets, and explicit dependencies over Wicked's supported paths. Done: a multi-pass authored scene rejects cycles and read/write hazards, reuses transient memory safely, and survives resize, suspension, and pass failure with reference captures.
- [ ] **R16 · P2 · GPU compute and indirect workloads** — After: F08, R13, R15.
  Expose only compute and indirect-draw operations supported by the pinned Wicked backend, with bounded dispatch descriptors, resource ownership, and completion tracking. Done: one real workload (such as culling or particle simulation) has a CPU fallback, capability negotiation, output validation, and measured cost on supported hardware.
- [ ] **R17 · P1 · Asynchronous GPU readback and capture** — After: F06, F10, R03, R15.
  Add a bounded staging/readback queue for screenshots, editor thumbnails, validation images, and GPU query results without blocking ordinary frames. Done: fence completion, cancellation, resize, queue saturation, and device failure are tested; captures identify their frame and dimensions and match a stable reference.
- [ ] **R18 · P2 · Multiple native windows and render surfaces** — After: F09, R03, R14.
  Own SDL3 windows and Wicked swapchains as independent generation-checked surfaces, each with its own resize, DPI, suspension, and close lifecycle. Done: an editor and a detached game view render simultaneously, and destroying either surface leaves the other and the game world usable.
- [ ] **R19 · P2 · Texture residency and mip streaming** — After: A04, A06, F08, R15.
  Connect cooked mip chains and queried format support to bounded asynchronous Wicked uploads, residency budgets, and eviction policy. Done: camera demand streams detail in and out without invalidating materials, exceeding configured memory, or stalling the render thread; unsupported formats use explicit tested fallbacks.
- [ ] **R20 · P1 · Render diagnostics and GPU budgets** — After: R02, R15.
  Report per-pass CPU/GPU timing, draw and upload counts, resource bytes, and frame markers through the engine's Tracy integration. Done: a representative scene produces reproducible warm/cold frame reports and regression thresholds, with unavailable GPU timings labeled rather than fabricated.

## P — physics and collision

- [ ] **P01 · P0 · Jolt ownership and stepping boundary** — After: F04, F07, R01.
  Decide from pinned source whether to expose Wicked's Jolt world or a separately controlled service; link one compatible Jolt build and disable competing simulation for managed bodies. Done: a fixed-step test proves each body advances exactly once and render updates do not secretly step it.
  Progress: `src/physics/policy.elisa` now owns a monotonic `StepClock` that rejects overlapping and non-contiguous commits, and `test/physics_policy.elisa` covers duplicate/skip prevention. `native/physics_body_bridge.h` now exposes a bounded fixed-tick scene step and the native gate proves a managed dynamic body advances across exactly one authoritative boundary per tick; kinematic targets, sleeping, and render-rate equivalence remain.
- [ ] **P02 · P1 · Body and shape lifecycle** — After: P01, F06.
  Expose static/dynamic/kinematic bodies, reusable primitive/mesh/compound shapes, mass properties, layers, and motion authority. Done: body/shape sharing, capacity failure, create/destroy, and world unload preserve identity and ownership without exposing Jolt IDs publicly.
  Progress: `src/physics/bodies.elisa` adds bounded generation-checked shapes and bodies, shared-shape lifetime protection, static/kinematic/dynamic authority, mass/layer validation, and unload counts; `test/physics_bodies.elisa` and `native/physics_body_bridge.h` are gated. Wicked now constructs typed static/kinematic/dynamic rigid-body components behind owner/generation-checked handles and proves stale/foreign rejection plus unload baseline; compound mesh cooking and broadphase layers remain.
- [ ] **P03 · P1 · Collision queries and events** — After: P02, W08.
  Implement raycasts, overlaps, shape casts, filters, triggers, and contact event queues with deterministic delivery policy. Done: nearest/all-hit queries, destroyed participants, overflow, and callback thread handoff are covered by real collision scenes and negative tests.
  Progress: `native/physics_query_bridge.h` now provides generation-checked native ray nearest/all-hit, sphere-overlap, and capsule-overlap queries with finite-input validation, caller layer/filter masks, fixed-capacity result storage, destroyed-participant rejection, and stale-token rejection; the Wicked native gate covers the real scene BVH. Shape casts, trigger/contact queues, overflow/event ordering, and callback thread handoff remain.
- [ ] **P04 · P1 · Fixed-step integration and interpolation** — After: P02, W02.
  Connect engine clock, substeps, kinematic targets, simulation output, and interpolated render poses with one writer per transform. Done: varied render rates produce equivalent fixed-tick results; pause, teleport, sleeping, and hitch limits behave as documented.
  Progress: `src/physics/interpolation.elisa` adds monotonic fixed-step pose publication, bounded alpha sampling, and one-sample teleport bypass; `test/physics_interpolation.elisa` covers fractional samples and duplicate ticks. `native/physics_interpolation_probe.h` mirrors the publication contract at the Wicked boundary and the native gate verifies fractional interpolation, duplicate rejection, and teleport bypass. Native Jolt pose commits, kinematic targets, sleeping, and render-rate equivalence remain.
- [ ] **P05 · P1 · Character controller** — After: P03, P04, I01.
  Implement supported Jolt character movement with slopes, steps, grounding, jump, crouch, moving platforms, and collision layers. Done: an interactive obstacle course has repeatable tests for corners, low ceilings, platform velocity, and frame-rate independence.
- [ ] **P06 · P1 · Constraints and interactable objects** — After: P02, P03.
  Add selected joints, limits, motors, break thresholds, sensors, and grab interactions through data-driven descriptors. Done: doors, lifts, and a jointed object run in an authored scene; deleting either endpoint safely invalidates dependent constraints.
- [ ] **P07 · P2 · Ragdoll and animated body blending** — After: P06, C04, R08.
  Cook skeleton-to-body mappings and controlled animation/ragdoll transitions with explicit ownership handoff. Done: impact, recovery, partial ragdoll, and despawn do not double-write transforms or retain stale skeleton/body references.
- [ ] **P08 · P2 · Robust physics settings and diagnostics** — After: P04, R06.
  Expose CCD, solver budgets, broadphase/layer diagnostics, material friction/restitution, and measured stability presets. Done: thin-wall projectiles, stacked bodies, fast movers, and large timesteps have reproducible outcomes and regression budgets.
- [ ] **P09 · P2 · Vehicles** — After: P05, P06.
  Wrap a supported Jolt vehicle path with suspension, wheel queries, drivetrain inputs, and render telemetry. Done: a drivable test track covers slopes, airborne wheels, collisions, reset, and clean unload; game policy remains Elisa-owned.
- [ ] **P10 · P2 · Box2D service and 2D sample** — After: F04, F06, W04, I01.
  Implement a genuine 2D physics service with independent units, shapes, bodies, contacts, queries, and joints. Done: a small authored 2D game uses it through the shared service model, with no accidental Jolt ownership or undocumented 2D/3D transform conversion.
- [ ] **P11 · P3 · Soft bodies and buoyancy** — After: P02, P08, R08.
  Expose supported soft-body and fluid-interaction capabilities behind separate feature flags and budgets. Done: one representative deformable and floating-body scene demonstrates render synchronization, stability limits, and lifecycle correctness rather than only a settings struct.
- [ ] **P12 · P2 · Large-world simulation strategy** — After: W05, P04, R11.
  Define double-precision simulation or origin rebasing based on actual library support; update rendering, audio, nav, and persistence conversions together. Done: travel far from origin preserves contact/picking precision and does not create a visible jump or corrupt saved coordinates.

## C — animation and character behavior

- [ ] **C01 · P1 · Skeleton and animation asset contract** — After: A05, F07.
  Define joint identity/order, rest and inverse-bind poses, clip tracks, events, units, and rig compatibility in cooked assets. Done: invalid parents, duplicate joints, missing tracks, and incompatible clips fail before runtime sampling.
- [ ] **C02 · P1 · ozz runtime animation service** — After: C01, F06.
  Promote `native/ozz_probe.h` into reusable clip/sampling-context/pose services with explicit scratch and output storage. Done: many characters sample independent clips/times without allocation per tick; the public Elisa API drives real rendered motion.
- [ ] **C03 · P1 · Animation graph** — After: C02, W08.
  Build Elisa-owned state machines, transitions, blend trees, parameters, and interrupt rules over library sampling. Done: an authored locomotion/action graph blends correctly across transition interruptions and reports invalid graph cycles or missing nodes.
- [ ] **C04 · P1 · Layers, masks, additive motion, root motion** — After: C03, P05.
  Implement per-joint masks, additive reference poses, event timing, and one root-motion authority shared with character movement. Done: upper-body actions preserve locomotion; loops, reverse playback, skipped frames, and collision-limited motion do not duplicate events or drift.
- [ ] **C05 · P1 · IK and foot placement** — After: C04, P03.
  Promote existing two-bone/aim logic and ozz facilities into bounded rig constraints with stable foot locking and ground queries. Done: a walking character handles uneven terrain without knee flips, sliding planted feet, or mismatched solver/render coordinate spaces.
- [ ] **C06 · P2 · Retargeting and sockets** — After: C01, C04.
  Add rig maps, retarget poses, proportion handling, and attachments driven by evaluated skeletons. Done: two different rigs share a clip with documented limits; a held item stays aligned during blends, teleports, and character destruction.
- [ ] **C07 · P2 · Animation compression with ACL** — After: C02, A02.
  Add an optional cooked codec path and benchmark it against existing ozz storage on real clips using positional/angular error budgets. Done: codec choice follows measured size/decode/error results, rejects incompatible data, and does not force two decoders into every build.
- [ ] **C08 · P1 · Character gameplay composition** — After: C04, C05, P05, R08.
  Provide a reusable Elisa character composition for movement, animation, camera, interactions, and lifecycle. Done: a third-person playground and the maze reuse the same services without native code deciding movement rules or animation state.
- [ ] **C09 · P2 · Character scale and update budgets** — After: C03, W07, R10.
  Add distance-based sampling rates, animation LOD, visible-pose priorities, and batched jobs while preserving event semantics. Done: a crowd benchmark reports pose cost/memory and compares throttled output to full-rate reference clips.
- [ ] **C10 · P3 · Full-body rig solver** — After: C05, C06.
  Implement an Elisa-owned constrained full-body solver only after defining a concrete multi-effector rig and convergence/error limits. Done: reach, balance, joint-limit, and conflicting-target scenes measure quality and cost against simpler IK; failure remains bounded and diagnosable.

## N — navigation and AI foundations

- [ ] **N01 · P1 · Recast bake pipeline** — After: A05, F07.
  Promote the Recast probe into deterministic agent-profile-aware navmesh cooking with area annotations and tile metadata. Done: a multi-room/stair/obstacle scene produces cached nav data, useful bake diagnostics, and debug overlays tied to source geometry. Progress: `native/navmesh_service.h` now provides bounded deterministic baking, source-generation metadata, serialized tile bytes, and failure-atomic Recast cleanup; multi-room/stair cooks, persistent cache loading, and debug overlays remain.
- [ ] **N02 · P1 · Detour runtime queries** — After: N01, A04.
  Add navmesh ownership, nearest-point, path corridor, raycast, reachability, area cost, and off-mesh link queries. Done: an agent follows real navmesh routes; partial/no-path outcomes, query capacity, stale tiles, and invalid endpoints are explicit. Progress: the reusable adapter now owns Detour mesh/query lifetimes and exercises nearest-point, routed path, raycast, filtered/no-path, invalid-input, and bounded-capacity statuses. `NavMeshTileStore` adds bounded generation-tagged publication/unload and rejects stale query handles in the native gate; multi-tile streaming, area costs, off-mesh links, and Elisa agent movement remain.
- [ ] **N03 · P1 · Agent movement and replanning** — After: N02, P05, W08.
  Keep intent and movement policy in Elisa; implement corridor following, stuck detection, path requests, and link traversal. Done: agents reach goals through doors/platform links, replan on obstruction, and cannot overwrite character/physics-owned transforms.
- [ ] **N04 · P1 · AI decision model** — After: N03, W08.
  Add typed reusable perception, blackboard/state-machine or behavior-tree primitives with bounded update work and debug state. Done: a patrol/chase/search/interact agent operates in the character playground and can be replayed and inspected from Elisa state.
- [ ] **N05 · P2 · Crowds and avoidance** — After: N03, W07.
  Integrate DetourCrowd where it fits the movement contract, with agent lifecycle and per-tick budgets. Done: converging crowds avoid obstacles and each other, recover from blocked exits, and retain bounded cost without bypassing gameplay or physics authority.
- [ ] **N06 · P2 · Dynamic and streamed navigation** — After: N02, W05.
  Integrate tile streaming and supported DetourTileCache obstacle updates with generation-tagged query results. Done: unload/rebuild during a pending path request cannot return stale routes; rebakes are bounded and agents recover when tiles become available.

## S — audio and acoustics

- [ ] **S01 · P1 · miniaudio engine service** — After: F04, F05, F06.
  Establish one native audio-device/mixer owner, disabling or isolating overlapping Wicked/FAudio playback paths. Done: reusable clip/voice/listener APIs play real sounds and handle device loss/reopen, initialization failure, and shutdown without leaked callbacks or duplicate output.
  Progress: `native/miniaudio_service.h` now owns one null-backed miniaudio device, bounded decoded clips, generation-checked voices, allocation-free callback mixing, explicit invalid-initialization cleanup, listener state, distance attenuation, device reopen, stale-handle rejection, and shutdown invalidation; streaming, occlusion, Doppler, and gameplay integration remain.
- [ ] **S02 · P1 · Streaming, buses, and voice budgets** — After: S01, A04.
  Add decoded/streamed assets, seek/loop, music/SFX/UI buses, gain ramps, priorities, virtualization, and voice limits. Done: long tracks stream within budget; cancellation, queue underrun, and voice stealing are tested without allocation or blocking IO in the audio callback.
  Progress: the bounded miniaudio service now exposes Music/SFX/UI buses, per-bus gain and voice budgets, priority-based deterministic voice stealing, and callback-side mixing with no allocation; streamed assets, seek/cancellation, gain ramps, virtualization policy, and underrun tests remain.
- [ ] **S03 · P1 · Spatial audio and world attachment** — After: S01, W02, P03.
  Implement listeners, distance/cone attenuation, velocity/Doppler policy, and basic occlusion using engine transforms and query results. Done: moving sources/listeners sound consistent with scene scale, and entity destruction safely detaches active voices.
  Progress: `src/audio/spatial.elisa` now owns bounded entity-keyed source attachment, listener/source velocity, distance and cone gain, occlusion, Doppler ratio, and detach/update validation; `test/audio_spatial.elisa` covers the portable policy. `native/miniaudio_service.h` now consumes source velocity and occlusion and exposes a bounded Doppler ratio in the real callback mixer, with native rejection tests. Transform/event integration and occlusion query plumbing remain.
- [ ] **S04 · P2 · Steam Audio integration** — After: S03, A11.
  Add opt-in HRTF and supported geometry-based acoustic effects with baked/runtime data and explicit worker budgets. Done: an indoor/outdoor scene compares enabled/fallback paths, async scene changes, device compatibility, CPU use, and audio latency.
- [ ] **S05 · P1 · Audio gameplay events and authoring** — After: S02, W08.
  Define sound event assets, weighted variants, concurrency groups, mixer snapshots, and animation/physics triggers. Done: footfalls, impacts, ambience, and music transitions use editor-authored data and do not double-fire during replay, pause, or reload.
- [ ] **S06 · P2 · Voice capture and Opus transport** — After: S02, T01, I03.
  Add explicit user-enabled microphone capture, Opus frames, jitter buffering, optional RNNoise, and session-scoped playback. Done: two processes exercise mute, device changes, packet loss, cleanup, latency, and bandwidth; capture never starts implicitly on scene load.
- [ ] **S07 · P2 · Audio regression harness** — After: S02, S03.
  Capture offline mixes and timing telemetry for gain, clipping, channel mapping, looping, and spatial fixtures. Done: deterministic DSP assertions complement audible/manual checks and expose underruns or callback overruns in long-session runs.

## I — input, UI, accessibility, and platform interaction

- [ ] **I01 · P0 · Action input and binding maps** — After: F09.
  Extend portable input to keyboard, mouse, gamepad, analog axes, dead zones, chords, rebinding, and gameplay/UI contexts. Done: hotplug, lost focus, held/released transitions, and replayed input behave consistently in the persistent native host.
  Progress: `src/runtime/action_input.elisa` now provides bounded context-aware bindings, analog dead-zone filtering, chord matching, per-frame edge transitions, and device disconnect cleanup; `test/action_input.elisa` covers the portable state machine. `native/action_input_bridge.h` now translates SDL3 keyboard events, applies analog values/deadzones, clears focus-loss state, and validates device reconnect behavior in the Wicked gate. Persistent rebinding storage, full SDL3 mouse/gamepad event translation, and replay injection remain.
- [ ] **I02 · P1 · UI rendering and interaction** — After: R03, I01, A04.
  Connect existing Elisa UI/layout/style models and suitable Elisa UI ecosystem modules to a persistent Wicked draw path, with clipping, scrolling, focus, and hit testing. Done: menus and HUD remain visible and interactive during gameplay rather than being removed before capture.
- [ ] **I03 · P1 · Platform services and settings** — After: F09, W06.
  Add user-data paths, clipboard, display/audio-device selection, graphics/input settings, and platform permission results. Done: saved settings apply safely on restart and reset/fallback behavior works when devices or displays disappear.
- [ ] **I04 · P1 · Unicode text and font assets** — After: I02, A04.
  Integrate FreeType/HarfBuzz with fallback fonts, glyph atlases, shaping caches, and justified ICU segmentation/bidi support. Done: mixed-direction, ligature, combining-mark, and fallback-font fixtures render and hit-test correctly without treating code units as glyph indices.
- [ ] **I05 · P1 · Editable text and IME** — After: I04, I01.
  Implement selection, caret movement, composition, clipboard operations, undo, and password-field policy using SDL3 text events. Done: multilingual IME composition and grapheme-aware deletion work at multiple DPI scales without leaking gameplay input.
- [ ] **I06 · P1 · Accessible game UI** — After: I02, I03.
  Add complete keyboard/controller navigation, scalable text, contrast themes, remapping, captions, reduced-motion settings, and semantic control metadata. Done: the sample can be configured and played without a mouse; platform accessibility bridges are reported separately from visual accessibility options.
- [ ] **I07 · P1 · UI data binding and reusable widgets** — After: I02, I05.
  Add typed model bindings, lists/tree views, sliders, inspectors, validation/error states, and virtualized large collections. Done: editor and game settings reuse the widgets; removal/reordering cannot leave stale focus or callbacks, and large lists remain bounded.
- [ ] **I08 · P2 · Vector graphics** — After: I02, A04.
  Integrate ThorVG for a concrete SVG/Lottie UI asset path with scale, animation, clipping, and cache lifetime policy. Done: a real HUD/editor panel consumes vector assets with validated rendering cost and graceful unsupported-format diagnostics.
- [ ] **I09 · P2 · Touch and haptics** — After: I01, I03.
  Add multi-pointer actions, gestures, controller vibration, and capability-based haptic output through SDL3. Done: touch/controller-specific examples handle cancellation and disconnect; unsupported devices preserve gameplay and disclose unavailable feedback.
- [ ] **I10 · P2 · Localization pipeline** — After: I04, E04.
  Define stable message keys, plural/locale formatting, extraction, translations, and live language switching. Done: a long-text and right-to-left locale exercise layout, missing-key fallbacks, and packaged locale data without rebuilding gameplay code.

## T — networking and online runtime

- [ ] **T01 · P1 · GameNetworkingSockets session service** — After: F04, F05, W08.
  Promote the loopback probe into server/client connection lifecycle, reliable/unreliable channels, backpressure, timeouts, and structured errors. Done: two independent processes connect, exchange gameplay data, disconnect/reconnect, and shut down cleanly; P2P/relay support is claimed only when actually configured and tested.
- [ ] **T02 · P2 · Versioned multiplayer protocol** — After: T01, W04.
  Extend the fixed demo frame into negotiated schemas, entity spawn/despawn, RPC/event limits, ownership, and authorization checks. Done: incompatible peers, malformed/oversized traffic, duplicate messages, and unauthorized state changes fail safely before world mutation.
- [ ] **T03 · P2 · Replication and interest management** — After: T02, W05.
  Implement subscriptions, relevance, baselines, deltas, acknowledgments, and per-client bandwidth budgets from Elisa world data. Done: separate clients receive only relevant entities; loss and late joining recover full state without stale references or unbounded queues.
- [ ] **T04 · P2 · Prediction, reconciliation, lag handling** — After: T03, P04, W09.
  Extend existing prediction/interpolation/recovery models to the real transport and character loop, defining solver determinism limits. Done: a two-process game tolerates configured delay/loss/reordering with measured correction error and bounded input/history buffers.
- [ ] **T05 · P2 · Dedicated headless server** — After: T02, P01, W07.
  Build a server target that links required simulation services without graphics, audio devices, or a display server. Done: remote clients play an authored level; tick overrun, shutdown, persistence, and metrics work on a clean machine.
- [ ] **T06 · P2 · Multiplayer test laboratory** — After: T03, T04, T05.
  Add deterministic transport fault injection, reconnect/late-join tests, traffic recording, protocol fuzzing, and long multi-client sessions. Done: failures include seed/trace/build identities and compare server/client state against explicit convergence bounds.
- [ ] **T07 · P2 · HTTP and downloadable content** — After: A03, A04, T01.
  Add cancellable libcurl-backed requests, bounded responses, progress, integrity-checked downloads, and atomic package installation. Done: timeout, resume, corrupted content, and interrupted updates preserve the last usable package; service credentials never enter logs or bundles.
- [ ] **T08 · P2 · Network operations and session UI** — After: T01, I07, Q02.
  Expose connection status, host/join workflow, errors, player roster, diagnostics, and configurable service discovery. Done: a packaged two-player sample is usable without editing source paths; hosted matchmaking/relay integrations remain explicit adapters with external prerequisites.

## E — editor, gameplay authoring, and iteration

- [ ] **E01 · P1 · Native editor shell** — After: F02, R03, I02.
  Build a persistent editor host with scene viewport, hierarchy, inspector, asset browser, console, and play controls over existing Elisa editor models. Done: a developer opens a project and runs gameplay without a Godot editor dependency or native hardcoded content.
- [ ] **E02 · P1 · Entity selection and transform tools** — After: E01, R06, W02.
  Add checked selection, multi-selection, translate/rotate/scale gizmos, snapping, local/world modes, and hierarchy reparenting. Done: edits are accurate under nested/nonuniform transforms and are reversible without leaking runtime IDs into saved scenes.
- [ ] **E03 · P1 · Transactional undo/redo** — After: E02, W04.
  Generalize existing field undo to create/delete/reparent, prefab overrides, and multi-object operations with bounded history. Done: undo after asset reload or selection deletion behaves predictably; failed commands leave the document unchanged.
- [ ] **E04 · P1 · Project and scene workflow** — After: E03, A02, W06.
  Implement project creation/open, scene save/load, autosave/recovery, recent projects, dirty indicators, and external-change handling. Done: a new project can author and reopen a scene with stable references after renames, crashes, and interrupted writes.
- [ ] **E05 · P1 · Asset browser and import UI** — After: E01, A02, A10, I07.
  Add thumbnail previews, type filtering, import settings, dependencies, diagnostics, drag/drop, and reimport progress. Done: an artist imports a model/texture/audio clip and places it in a scene; missing/broken assets remain repairable instead of disappearing silently.
- [ ] **E06 · P1 · Play-in-editor isolation** — After: E04, F05, W04.
  Clone/remap an authored world for play, support pause/step/restart, and define explicit apply-back rules. Done: repeated play sessions neither change the saved world nor leak native resources; the user can inspect runtime state and return to editing safely.
- [ ] **E07 · P1 · Material and environment editors** — After: E05, R04, R05.
  Add live material previews, texture-slot editing, lighting/environment controls, and quality settings using runtime descriptors. Done: saving a material updates all intended instances through generation-safe reload; invalid edits preserve the last valid render state.
- [ ] **E08 · P2 · Animation and rig authoring** — After: E05, C03, C05.
  Add clip preview, transition/blend editing, event tracks, masks, IK targets, and pose diagnostics. Done: an artist builds and saves a locomotion graph that the standalone runtime evaluates identically without editor-only data dependencies.
- [ ] **E09 · P1 · Physics/nav/audio authoring** — After: E05, P06, N02, S05.
  Expose colliders/joints/layers, nav areas/links/bake controls, listeners/sources, and sound events with matching debug visualization. Done: a level assembled through the editor is playable with collisions, navigation, and audio in a packaged native build.
- [ ] **E10 · P2 · Safe code iteration and extension APIs** — After: E06, W08, F05.
  First provide rebuild-and-restart preserving authored state; add native code reload only after proving quiescence, callback drain, ABI compatibility, and state migration. Done: old code cannot remain callable after replacement; incompatible changes fall back to a controlled restart with clear diagnostics.

## Q — performance, testing, packaging, and platforms

- [ ] **Q01 · P0 · Representative native regression scenes** — After: F10, R01.
  Keep the maze and add independent fixtures for resource churn, many instances, lit materials, and interactive lifecycle behavior. Done: native tests exercise real services with meaningful assertions and structured failure artifacts; scene counts or nonblank pixels alone are insufficient.
- [ ] **Q02 · P1 · Standalone Release packaging** — After: F03, F05, A03, R13.
  Package executable, runtime libraries, shaders, assets, settings defaults, and notices with relocatable paths. Done: a Release game runs offline outside the repository on a clean target machine, without Homebrew/source-tree paths or runtime downloading build dependencies.
- [ ] **Q03 · P1 · Native CI and toolchain bootstrap** — After: F03, F10, Q01.
  Extend current CI with pinned compiler/prover/ElisaScript provisioning, native contract tests, and artifact retention; separate GPU workstation jobs from hosted headless jobs. Done: missing tools fail visibly, caches are identity-keyed, and every claimed platform has its own execution evidence.
- [ ] **Q04 · P1 · Crash and actionable diagnostics** — After: F05, Q02.
  Add bounded structured logs, source/build IDs, symbolized crash artifacts, reproducible launch arguments, and optional explicitly configured Sentry Native integration. Done: a packaged induced failure yields usable local diagnostics; telemetry requires configuration and excludes private project/user content by default.
- [ ] **Q05 · P2 · Tracy and engine performance budgets** — After: W07, R10, C09.
  Instrument simulation, jobs, IO, uploads, GPU frames, audio, locks, and allocation lifetimes; establish scene/hardware-specific regression budgets. Done: optimized-build traces locate bottlenecks, include cold/warm runs, and distinguish profiling overhead from engine cost.
- [ ] **Q06 · P2 · Soak, stress, and sanitizer coverage** — After: F05, W05, A10, T06.
  Automate repeated load/unload, reload, connect/disconnect, resize, and input stress with fault injection. Done: multi-hour runs have bounded resources and usable reproduction traces; ASan/UBSan and supported thread/GPU validation paths cover real adapters, with unsupported checks disclosed.
- [ ] **Q07 · P1 · Second authored game and SDK example** — After: E06, P05, C08, S05, Q02.
  Build a small third-person exploration/combat game using public APIs and editor-authored assets rather than maze-specific shims. Done: documented new-project-to-package steps work; any sample-only native gameplay logic is moved into reusable Elisa services or the game's Elisa code.
- [ ] **Q08 · P2 · Windows and Linux native targets** — After: Q02, Q03, F09.
  Implement/test platform shims, dependency builds, SDL3 windows, shader compilation, and the Wicked graphics path appropriate to each target. Done: the same authored sample passes startup, gameplay, input, rendering, resource teardown, and packaging on actual Windows and Linux environments.
- [ ] **Q09 · P2 · Distribution readiness** — After: Q02, Q08.
  Generate dependency/license notices and a component inventory, review redistribution requirements, and prepare platform signing/install/update workflows. Done: artifacts are reproducible and attributable; the project license and production signing credentials remain explicit owner decisions, never invented by an agent.

## X — specialist expansion with concrete consumers

- [ ] **X01 · P2 · Procedural world generation** — After: R11, W05, A02.
  Integrate FastNoise2 through deterministic versioned generator assets for terrain/biomes/caves with bounded jobs and cached output. Done: a seeded traversable world regenerates reproducibly, streams within budget, and shares geometry with collision and navigation.
- [ ] **X02 · P3 · XR runtime** — After: F08, R03, I09, Q02.
  Build an optional OpenXR service for session lifecycle, views, predicted poses, actions, and device loss, after confirming the chosen Wicked target's support. Done: a real headset sample validates timing, tracking-origin conventions, stereo rendering, and clean fallback when no runtime exists.
- [ ] **X03 · P3 · Cinematics and video** — After: S02, R03, E04.
  Add an Elisa timeline for cameras, animation, audio, events, and skippable sequences; integrate FFmpeg only for a concrete video/import path. Done: seeking, pause, skip, AV synchronization, resource release, and distribution requirements are demonstrated in a packaged sequence.
- [ ] **X04 · P3 · Production image and color pipeline** — After: A06, R04, E07.
  Add justified OpenImageIO/OpenColorIO offline import and color-transform stages with explicit working/display spaces. Done: HDR/EXR and reference-chart fixtures survive cook-to-display with documented precision, metadata, and renderer limitations.
- [ ] **X05 · P3 · MaterialX and USD interchange** — After: A05, E04, E07.
  Implement bounded supported subsets that translate into engine-owned scene/material assets, keeping heavy interchange tools offline where possible. Done: one real production asset round-trips supported data, and unsupported graph/schema features are diagnosed without implying full compatibility.
- [ ] **X06 · P3 · Sparse volumes** — After: X01, A04, R12.
  Add an OpenVDB/NanoVDB asset route only with a defined volume-rendering/query consumer supported by the pinned renderer or a reviewed extension. Done: a cloud/voxel example demonstrates streaming, transforms, GPU/CPU budgets, and explicit unsupported-target behavior.
- [ ] **X07 · P3 · External VFX authoring** — After: R09, E05.
  Integrate Effekseer only when a representative effect cannot be reasonably authored through the Wicked path; define lifetime and renderer submission ownership first. Done: an authored effect responds to Elisa events, reloads, packages, and tears down without a second gameplay clock.

## Validation and handoff

Use the existing commands as regression anchors; F10 adds native-specific composition:

- `elisascript scripts/check.elisascript` — shared runtime, rejection, proof, and package evidence;
  currently includes a Godot probe, so it is not yet the native-only gate.
- `elisascript scripts/wicked_probe.elisascript` — real native graphics and library evidence;
  requires the configured Wicked build and a suitable graphics session.
- `python3 scripts/run_boundary_sanitized.py` — untrusted native boundary checks.
- `python3 test/check_workflow.py ~/.local/bin/elisascript` — orchestration behavior only.
- `python3 scripts/check_module_hygiene.py` and `python3 scripts/check_source_length.py` — repository policies;
  also check this new root plan's length until F10 includes it in the policy inventory.
- When changing compiler field/reference semantics, run the owning compiler's targeted
  regressions and `test/parity/driver_acceptance_smoke.sh`; do not raise its baseline to hide regressions.

Every handoff names: completed task IDs, commits/repos, commands and outcomes, evidence
files, API changes, remaining limitations, and the next ready task. Maintain separate
statuses for implemented, integrated, tested, proved, hardware-unverified, and blocked.
No number of checked boxes substitutes for the milestone's playable and packaged result.
