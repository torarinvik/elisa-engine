# Native integration inventory

**Recorded:** 2026-09-19
**Engine commit inspected:** `89976f3`
**Purpose:** F01 evidence for the native implementation plan.

This is an inventory of the current integration depth. The labels are deliberately
stricter than “the header links”:

| Label | Meaning |
|---|---|
| Policy | Elisa describes ownership or a choice, but no native service consumes it. |
| Probe | A bounded check or finite diagnostic uses the library. |
| Adapter | A reusable boundary exists, but it is not yet a complete game service. |
| Integrated | A real host path uses the service for gameplay or authoring and has outcome checks. |

## Host and ownership map

| Area | Entry points | Owner | Current label | Gap to reusable engine |
|---|---|---|---|---|
| Elisa world and identity | `src/world/`, `src/entity_id.elisa`, `src/backend/scene_bridge.elisa` | Elisa | Integrated for the maze slice | General world storage, streaming, save schema, and render extraction are still roadmap work. |
| Native Wicked host | `native/wicked_probe.cpp`, `scripts/wicked_probe.elisascript` | Native shim, with Elisa state as input | Probe | It exits after a finite diagnostic run and still contains maze-specific scene construction. |
| Embedded C ABI | `examples/maze/capi.elisa`, `native/embed_probe.cpp`, `scripts/embed_probe.py` | Elisa API plus narrow C shim | Adapter | Version negotiation, spans, malformed descriptors, and a persistent client belong to F04/F02. |
| SDL3 platform | `src/backend/sdl3.elisa`, `native/input_probe.h`, `native/wicked_probe.cpp` | SDL3 host boundary | Adapter | Focus, resize, suspension, frame pacing, and restart are incomplete in F09. |
| Godot compatibility host | `backends/godot/`, `scripts/godot_capture.elisascript` | Godot adapter | Integrated probe | Compatibility remains useful, but it is not allowed to define the native gate or block native-only features. |
| Validation orchestration | `scripts/check.elisascript`, `scripts/record_validation.py` | Repository tooling | Integrated checks | Native-only composition and explicit hardware-unverified results belong to F10. |

## Native libraries and exact identities

The exact identities below are recorded by the dependency manifest or fetch
scripts. `scripts/check_dependency_manifest.py` verifies pinned revisions and
required artifacts before the native driver starts.

| Library | Identity in this checkout | Build/use entry point | Label | Owner and missing work |
|---|---|---|---|---|
| WickedEngine | sibling checkout `857d1705ce499a13b9668b6ccab185b2bf249689`; includes command-list/job-drain fixes plus Metal dispatch-data and FAudio engine/reverb releases | `scripts/wicked_probe.elisascript`, `native/wicked_probe.cpp` | Probe | Native backend owner; F05 still has process heap high-water growth across repeated device cycles and macOS framework cycles. |
| SDL3 | Homebrew/system install selected by `WICKED_SDL3_INCLUDE_DIR` and `WICKED_SDL3_LIB_DIR`; presence is machine-specific | `src/backend/sdl3.elisa`, Wicked driver | Adapter | Platform owner; F03 records a reproducible toolchain and F09 completes lifecycle behavior. |
| Jolt | Linked transitively from Wicked `libJolt.a` | `native/wicked_probe.cpp` physics path | Probe | Physics owner; P01 makes one explicit Elisa-owned step/query service. |
| cgltf | `snapshot-2026-09-18`, SHA-256 `efb169dee911696b5d35fc8e3f7ea0c56d679debc529eba9ca6aa6443ba9d5e9` | `native/asset_import.h`, `scripts/cook_assets.py` | Adapter | Asset owner; A05 completes normalized scene, skin, morph, and animation import. |
| meshoptimizer | `v1.2`; hashes in `scripts/fetch_dependencies.py` for header, allocator, vcache, analyzer | `native/meshopt_probe.h`, Wicked driver | Probe | Asset/render owner; A07 connects optimization and LOD output to a consuming render path. |
| KTX2/BasisU | BasisU commit `99f52d63aa6799cbdaecfe977111dc5ec3b31d47` | `scripts/fetch_basisu.py`, `native/basisu_probe.cpp`, texture probes | Probe | Asset owner; A06 completes queried format selection and runtime residency. |
| ozz-animation | commit `6cbdc790123aa4731d82e255df187b3a8a808256` | `native/ozz_probe.h`, `native/pose_probe.h`, `native/skin_probe.h` | Probe | Animation owner; C02/C03 provide reusable clips, graphs, and buffering. |
| Recast/Detour | commit `6dc1667f580357e8a2154c28b7867bea7e8ad3a7` | `native/navmesh_service.h`, `native/recast_probe.h` | Adapter | Bounded bake/query ownership is integrated into the native gate; N01–N04 still add cooked multi-tile assets, generation-tagged streaming, agents, and replanning. |
| miniaudio | `0.11.22`, SHA-256 `9019743287e443c55e5737a7297f38e5e358561701d6db2d905afb114390c410` | `native/miniaudio_service.h`, `native/miniaudio_probe.h`, `native/audio_probe.h` | Adapter + Probe | One-device bounded clip/voice owner is integrated; S01–S03 still add device recovery, streaming, buses, and spatial attachment. |
| Tracy | commit `30997d5ca6bb632cc10807a1da8a6d3de0aeeb3c` | `native/tracy_probe.h`, Wicked driver | Probe | Performance owner; Q05 turns marks into budgets and reports. |
| zstd | Host/library link plus `native/zstd_probe.h` | Wicked driver and package checks | Probe | Asset owner; A03 makes package format bounded, indexed, override-resolvable, and runtime-usable. |
| FreeType/HarfBuzz/ICU | Host package-manager libraries; no content hash currently recorded | `native/text_probe.h`, `src/ui/text.elisa` | Probe/Policy | UI owner; I04/I05 add shaping, font residency, editing, and localization policy. |
| GameNetworkingSockets | commit `a424b7db649438acafb60c99cae6667587c42732` | `native/gns_probe.cpp`, `src/net/` | Probe | Network owner; T01/T03 turn loopback transport into a two-process session and replication. |
| UDP sockets | Platform API | `native/udp_probe.h`, `src/net/transport.elisa` | Adapter | Network owner; retain as a diagnostic transport while GNS owns production sessions. |
| SQLite | Platform/library dependency | `src/assets/database.elisa`, catalogue tests | Adapter | Asset owner; A02 adds transactional dependency graph, recovery, and deterministic cache. `src/assets/loader.elisa` supplies the portable request/residency contract for A04's native worker and upload queues. |
| Box2D | Listed in `dependencies.md`, no current native consumer | Planned P10 | Policy | Physics owner; a real 2D example is required before calling this integrated. |
| Steam Audio | Listed in `dependencies.md`, no current native consumer | Planned S04 | Policy | Audio owner; integrate only after the miniaudio spatial service has a concrete use. |
| ufbx | Listed in `dependencies.md`, no current native consumer | Planned A09 | Policy | Asset owner; normalized FBX route follows the glTF contract. |
| ACL | Listed in `dependencies.md`, no current native consumer | Planned C07 | Policy | Animation owner; benchmark against the current codec before adding a second path. |
| OpenXR, MaterialX/USD, OpenVDB, Effekseer, FFmpeg, OIIO/OCIO | Listed in `dependencies.md`, no current native consumer | Planned X03–X07 | Policy | Each remains gated by a concrete consuming feature and an authored example. |

## Commands and evidence

These commands are the current entry points. They identify what is actually
checked and what remains unverified; they do not imply that every command is
available on every workstation.

| Command | What it proves now | Limitation |
|---|---|---|
| `elisascript scripts/check.elisascript` | Elisa compiler, tests, proofs, package checks, and Godot compatibility gate | It is not native-only and does not prove a persistent Wicked application. |
| `elisascript scripts/wicked_probe.elisascript` | SDL3/Metal Wicked frame, canonical scene consumption, selected library probes, capture and frame budget | Requires sibling Wicked build, native libraries, display/GPU, and still runs a finite probe. |
| `python3 scripts/run_boundary_sanitized.py` | Untrusted boundary harness under sanitizers | It deliberately excludes the graphics driver and is not a complete engine soak. |
| `python3 scripts/check_module_hygiene.py` | Namespace, owner-field, and constructor hygiene policy | Static policy; it cannot prove runtime ownership or vendor lifetime. |
| `python3 scripts/check_source_length.py` | Source/document line limits | Until F10, the new plan length is checked manually. |
| `python3 scripts/package_release.py` | Deterministic release archive for the current maze slice | It packages the probe-era vertical slice, not a general authored game. |

## Priority gaps

The inventory makes the first implementation sequence concrete:

1. F02 extracts a persistent native application from the finite probe and makes
   the probe a client of that application.
2. F03 records all native toolchain and dependency identities, including SDL3,
   Wicked patches, compiler, SDK, and build options.
3. F04 defines the versioned service ABI before more vendor types reach Elisa.
4. F05 removes normal forced exit and proves repeated startup, restart, and
   shutdown across partial initialization failures.
5. F06–F09 generalize handles, coordinate rules, capabilities, and SDL3 frame
   lifecycle before adding the larger rendering and world services.

The current labels intentionally preserve historical probe evidence while
preventing a linked library or one rendered maze from being mistaken for a
complete engine subsystem.
