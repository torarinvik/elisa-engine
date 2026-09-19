# Native backend validation

This record summarizes executable evidence for the SDL3 native boundary, the
Wicked Metal host, and the Godot host. Detailed capability labels and repeatable
commands live in [`docs/capabilities.md`](capabilities.md). The upstream Wicked
checkout stays beside the repository so third-party source and build output do
not become Elisa source.

## Current status — 2026-09-19

The native probe consumes the canonical Elisa scene, drives the maze through
SDL3 input, renders hidden Metal frames, checks deterministic captures, and
exercises lifecycle churn, authored assets, animation, navigation, audio, UI,
and the frame budget. Godot runs the corresponding embedded session and the
cross-host semantic comparison. Both hosts rebuild backend resources from the
same Elisa-owned state.

The initial renderer triage is historical. The present probe is the SDL3/Metal
rendered host used by the validation workflow.

The SDL3 window and Wicked application boundary now live in
`native/native_application.h`. `native/wicked_probe.cpp` consumes that
`NativeApplication` lifecycle instead of creating SDL and Wicked state inline;
the probe still deliberately runs a finite diagnostic client and uses the
existing forced-exit workaround until F05 supplies orderly Wicked teardown.

After device initialization the native gate queries the adapter name, shader
format, viewport limit, video-memory budget/usage, mesh-shader, ray-tracing,
and sparse-texture capabilities through Wicked's graphics device. Optional
features are reported as `native` or `fallback`; they are not inferred from a
linked library or from the machine-independent Elisa profile.

The SDL3 host records logical and physical window sizes, display changes,
focus, minimize, restore, and close transitions. A minimized or zero-pixel
window suspends simulation while the event queue remains live, so input edges
are not lost during a resize. `native/frame_pacer.h` keeps simulation ticks at
an integer nanosecond step, caps catch-up at four ticks, and exposes the
presentation interpolation fraction without adding work to the Elisa world.
The finite native gate injects focus/minimize/restore/pixel-size events and
checks the state transitions and monotonic resize serial.

`NativeApplication::shutdown()` provides the host-local half of orderly
shutdown (GPU wait, window detachment, SDL destruction). The optional
`ELISA_ORDERLY_SHUTDOWN=1` probe is intentionally not part of the green gate:
the pinned Wicked worker systems still lack a public process-wide shutdown and
the full graphics run does not complete that mode reliably. This limitation is
tracked in [`docs/validation/orderly-shutdown.md`](validation/orderly-shutdown.md)
and keeps F05 open.

## Prerequisites and commands

Godot 4.7.2 is used for the headless host probe:

```sh
godot --headless --path backends/godot --script backends/godot/probe.gd
```

The Wicked checkout is pinned at revision
`5e07e3bfd7f89633a468009e0620b14e508dd8b7`. The arm64 Debug/O0 build provides
Wicked, Jolt, Utility, FAudio, and Lua. The native driver is:

```sh
elisascript scripts/wicked_probe.elisascript
```

The driver accepts `WICKED_ROOT`, `WICKED_BUILD`, and `ELISA_SDL3_LIB_DIR`
overrides. It builds the native probe, runs the SDL3 platform boundary, saves
the rendered frame, checks dimensions and blank-frame rejection, and compares
canonical structural markers. `scripts/godot_capture.elisascript` performs the
corresponding Godot capture and comparison.

Before compiling, the driver runs `scripts/check_dependency_manifest.py` against
`native/dependency-manifest.json`. That preflight verifies the current Wicked
CMake cache selected SDL3, checks git revisions and content hashes for required
artifacts, and rejects SDL2 tokens in the active native target. It reports the
configured machine's exact paths while keeping third-party files outside the
engine checkout.

The full workstation gate is:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_ALLOW_STALE_STAGE1=1 \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/scripts/elisac_stage1.sh" \
~/.local/bin/elisascript scripts/check.elisascript
```

## Host contracts

`src/backend/scene_bridge.elisa` validates one command stream: create, update,
destroy, viewport, epoch, identity, and asset references. The recorder rejects
updates before creation, duplicate creation, stale references, commands after
destruction, and capacity overflow without appending partial state.

The Godot host creates real `MeshInstance3D`, `StandardMaterial3D`, and
`Camera3D` resources. Its embedded extension drives the same Elisa C ABI used
by the native embedding host. The capture checks cooked geometry, RGBA, packed,
BC1, KTX, and KTX2/Basis textures; audio decode and cue count; menu state and
style; pose and skinning data; input mapping; live input; deterministic frames;
and teardown. It requires the scene root to return to its environment node
after unload.

The C embedding boundary also exposes a versioned `ElisaServiceV1` table for
session create/update/query/destroy. The host uses opaque generation handles,
bounded input/output spans, explicit callback affinity, and a caller-provided
allocator contract. `scripts/embed_probe.py` drives a real generated Elisa
archive through that table and rejects version, size, allocator, stale-handle,
and undersized-buffer failures before gameplay continues.

The native host creates the canonical maze geometry, applies SDL3 events to
portable Elisa actions, submits Wicked transforms and meshes, and removes all
created objects. The probe checks Jolt ownership, scene resource counts,
deterministic capture, cooked assets, audio, UI, and the measured frame budget.
Before building the maze scene it also creates and destroys real Wicked cube
resources through `NativeResourceRegistry`, checks generation reuse, rejects a
stale handle, and rejects a handle passed to another scene. The registry is a
logical lifetime layer; GPU-fence retirement is still a later F06 step.
The shared native conversion in `native/coordinate_conventions.h` owns the
right-handed Elisa-to-left-handed Wicked X flip and the grid spacing/origin;
walls, markers, live input, and route replay all use that one conversion.

The offline cooker records each package in a versioned SQLite catalogue. Its
WAL journal and immediate write transaction make a crashed cook roll back
without corrupting prior rows; source/content/settings keys and a unique cache
key make repeated or concurrent requests converge on one ready artifact. The
catalogue also stores embedded-source dependencies and a diagnostic row. The
cooker self-test opens the database again after a rollback and checks the
single-row cache invariant, while `record_validation_assets.py` checks the
schema and dependency/cache rows produced by the real maze cook.

For manual host work, set `ELISA_PERSISTENT_HOST=1` before launching the built
probe. The same scene then opens as a visible client and runs until SDL close;
W/A/S/D reaches Elisa through the C ABI, P pauses input, and R restarts the
Elisa game state. The default validation invocation remains finite. Persistent
mode still ends at the known forced-exit boundary until F05 can prove Wicked's
ordered global teardown, and it requires an interactive graphics session.

## Render comparison

`src/backend/image_compare.elisa` defines per-channel peak and mean tolerances.
`scripts/compare_renders.py` parses PNG files with the Python standard library,
checks dimensions, rejects blank frames, and samples the projected maze grid
for structural agreement. Byte-identical pixels are not required because the
hosts use different renderer color paths; semantic scene agreement is the
portable contract.

The native frame is captured at the physical Retina scale. The comparison
accepts integer dimension scaling and checks marker colors for player, key,
door, hazards, goal, fog, and status. A live-input capture must differ from
the idle state and remain non-blank.

## Gameplay and lifecycle evidence

The maze vertical slice covers movement, collision, key and door rules,
hazards, goal, lives, restart, pause, settings, fog, and exit. The character
example owns a World identity, navigates with the Elisa route policy, evaluates
animation and two-bone IK, and unloads by despawning its entity. A stale
reference is rejected after unload.

The native churn probe runs eight batches of 64 create/remove operations and
requires object, mesh, and transform counts to return to baseline. The Godot
capture frees every geometry node and physics body, waits two frames, and
requires the scene root to contain only its environment node. Restart and
unload failures therefore remain visible instead of relying on process exit.

## Asset, animation, and specialist boundaries

The cooker validates bounded glTF input, writes the versioned package, records
the SQLite catalogue, and emits RGBA, RGB565, BC1, KTX, and optional KTX2/Basis
textures. The native probe imports through cgltf, cache-optimizes indices with
meshoptimizer, reloads the package, and verifies byte-exact decoded data.
Godot consumes the same cooked geometry and compressed texture paths.

The native probe links pinned ozz-animation and samples a two-joint clip at
start, midpoint, and end against the Elisa sampler. Recast/Detour builds and
queries a navmesh around a wall and gap. miniaudio decodes the test WAV and
opens a null device. FreeType rasterizes a glyph and HarfBuzz shapes “Elisa”.
zstd round-trips the actual cooked package. Tracy is compiled with frame marks;
without a server, the evidence is that the client is linked and exercised.

## Networking and release boundaries

The Elisa net modules own frame encoding, authority, prediction, interpolation,
recovery, rollback, and reliable delivery. UDP proves the OS socket boundary;
the pinned GameNetworkingSockets probe moves the 33-byte replication frame over
a real loopback connection. Transport behavior stays below the Elisa policy.

`scripts/package_release.py` writes the packaged headless game, cooked assets,
fixture, C ABI, and Godot extension twice and requires byte-identical archives.
`build/validation.json` records the engine source manifest, tool identities,
dependency pins, proof certificates, release hash, and source policies.

## Sanitizers and known scope

`scripts/run_boundary_sanitized.py` runs the Wicked-free boundary harness under
AddressSanitizer and UBSan. It covers ozz, Recast/Detour, miniaudio, and
FreeType/HarfBuzz with no findings. The full graphics sanitizer probe remains
a separate workstation command because graphics-driver environments can abort
before producing a sanitizer report; that result is never presented as a
boundary-library pass.

The plan deliberately leaves Box2D, ACL, Steam Audio, Effekseer, ufbx, and
arbitrary code hot reload gated on a representative use case or a complete
quiescence and migration design. A project license still requires an explicit
product decision before distribution. These are documented scope boundaries,
not hidden backend substitutions.
