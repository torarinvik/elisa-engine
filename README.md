# Elisa Engine

An experimental Elisa-native game engine built around algebraic entity hierarchies,
numeric identity, and specialist engine libraries. SDL3 and headless Godot and
Wicked host probes are now exercised, and the Godot host embeds Elisa gameplay
through a hand-written GDExtension over the same C ABI the native host uses
(ADR-0013); the full Wicked renderer remains planned.
[Native backend validation](docs/native-backend-validation.md)
records the pinned external Wicked checkout and its macOS build notes.
Binding architecture decisions live in [docs/adr/](docs/adr/); anything the
ADRs mark as not covered is not claimed anywhere else in this file. Every
capability is labelled proved, tested, implemented, partial, or planned with
its evidence in [docs/capabilities.md](docs/capabilities.md).

## Current foundation: identity and checked world

`src/entity_id.elisa` supplies the `EntityId` module's affine, world-local numeric
identity allocator. Import it with `using EntityId`, initialize
`EntityIdAllocator{last_issued: ENTITY_ID_INVALID}`, and call
`entity_id_allocate(&allocator)` with `catch` to handle its error union.
Successful allocation returns IDs from 1 through `ENTITY_ID_MAX`.
`ENTITY_ID_INVALID` (zero) is the unissued initial cursor, not a failure result.
Exhaustion raises `EntityIdError.MaxId`; negative allocator state raises
`EntityIdError.InvalidId`. Both errors leave the cursor unchanged.
The initial representation uses positive `i64` values; a packed handle ABI is not fixed.

IDs are never recycled within one allocator lifetime. `src/world/world.elisa`
adds an affine `World` with one allocator, a live registry, separate actor/enemy
stores, and an epoch in each `EntityRef`. Spawn, despawn, checked lookup, and
compaction run through that world. Numeric IDs may coincide across worlds; the
epoch distinguishes their references. The current serial, fixed-capacity world
is an early implementation, not yet a persistent asset identity system.

`src/math/geometry.elisa` defines backend-neutral vector, quaternion, transform,
bounds, and ray values. Its convention is metres in a right-handed frame, +Y up
and -Z forward. The initial operations cover translation, vector arithmetic,
point-in-bounds, bounds overlap, and ray evaluation; quaternion composition and
matrix conversion remain future work.

`src/backend/recording.elisa` defines value-only scene commands and a bounded
headless recorder. It rejects updates before creation, duplicate creation,
commands after destruction, invalid references, and capacity overflow without
appending a partial command. Each command carries the world epoch, entity ID,
and stable mesh/material asset IDs from `src/assets/descriptor.elisa`;
the backend has no authority to create or recycle either identity. This is a
test backend, not a Wicked or Godot integration.

`src/backend/contracts.elisa` records the ownership, retention, alignment,
thread, failure, and cleanup policy for the first six native bridge operations:
immediate shared input, in-place output, asynchronous upload, resource creation,
callback registration, and destruction. The metadata is validated as data and
does not claim to make unverified C pointers safe.

The asset module separates authored asset IDs from world-local entity IDs and
backend handles. It validates source descriptors and portable visual references,
cooks validated visuals into content-hashed packages, tracks catalogue
generations with stale-package rejection, and binds everything into a
shippable maze bundle (assets, scene identity, required capabilities,
determinism scope, frame budget). Byte-level importers, transcoding, and
streaming remain future work. `src/runtime/input.elisa`
maps portable keyboard/controller buttons to gameplay actions and rejects
ambiguous bindings. A host still needs to translate platform-specific events
to those button values.

`src/runtime/clock.elisa` advances simulation in fixed 16,667-microsecond ticks,
with at most four catch-up ticks per call. `src/runtime/headless_game.elisa`
applies mapped input to an Elisa-owned transform and records the resulting
commands. Two independent runs with the same inputs produce matching command
traces in the headless test. A full recorder rejects an update before changing
the clock or transform; a failed player spawn removes its World liveness while
allowing the allocated ID to remain burned.

`src/backend/capabilities.elisa` gives a selected host an explicit capability
profile and checks a game's required feature list before startup. The headless
profile intentionally supplies input only; native and Godot hosts must publish
their actual capabilities when those integrations exist.
`src/backend/scene_bridge.elisa` validates the one canonical command stream
both hosts consume (epoch, identity, create/update/destroy order, viewport),
and `src/backend/image_compare.elisa` compares rendered output under an
explicit per-channel plus mean tolerance instead of byte equality.

`src/backend/fake_bridge.elisa` is a deterministic bridge test double. It checks
partial-create cleanup, generation-checked handles, asynchronous upload
retention, resource destruction ordering, and callback unregister/drain rules
before an actual native library is introduced.

`src/backend/sdl3.elisa` is the first real native platform boundary. It declares
only the SDL3 calls needed by the probe, owns the SDL window resource through an
Elisa drop hook, and links the test against Homebrew's SDL3 library. It does not
expose SDL pointers or make a renderer claim; the probe verifies event-system
initialization and safely attempts a hidden window.

`backends/godot/project.godot` and `probe.gd` provide the first Godot host smoke
test. Godot applies a portable create/update/destroy sequence to a real
`MeshInstance3D`, material, and camera, then checks epoch/entity identity and
lifecycle ordering in headless mode. This is a host-contract probe, not the
full GDExtension backend; the Elisa world remains the authoritative simulation.

`native/wicked_probe.cpp` and `scripts/wicked_probe.elisascript` provide the
optional native Wicked scene probe. It creates an Elisa-named cube and camera,
updates the transform, renders one hidden Metal frame through Wicked's
`RenderPath3D`, and checks despawn against the external static libraries without
copying WickedEngine into this repository.

`examples/maze/assets/maze_tile.gltf` is the first authored source
asset; the Godot host loads it through its own glTF importer and the
native host imports it through cgltf, both verified against the
fixture's pinned triangle count. `scripts/cook_assets.py` bounds untrusted import input (document, buffer,
accessor, and mesh counts; accessor byte ranges inside the buffer) and rejects a
malformed or oversized asset rather than partially parsing it; its `--self-test`
rejects a set of crafted bad documents and runs as part of validation. It also records a persistent
SQLite catalogue of cooked assets (source path, content hash, counts) for the
toolkit, distinct from the runtime package, and cooks a first texture: a small
solid-green texture in a versioned texture package that both hosts upload and
apply to the goal material (RGBA for the material, plus 16-bit R5G6B5 and a
BC1/DXT1 block-compressed block for the compressed path), so the authored
texture reaches the rendered frame; the Godot capture's goal-colour check and
the cross-host comparison both still pass. It then
cooks the source into a versioned, hash-carrying package (normalized float32 positions/normals and uint32
indices) as part of the validation run, and both hosts build the goal
marker's mesh from that package rather than from a source format. Fetch the
pinned dependencies with `python3 scripts/fetch_dependencies.py` (cgltf and the
meshoptimizer vertex-cache files, revision- and hash-locked) and
`python3 scripts/fetch_ozz.py` (ozz-animation, pinned by commit and built into
`dependencies/ozz`) and `python3 scripts/fetch_recast.py` (Recast/Detour,
pinned by commit and built into `dependencies/recast`; miniaudio is a pinned
single header) before building the native probe. The native host
cache-optimizes the cooked index buffer with meshoptimizer before upload —
failing the probe if the optimizer worsens the measured average cache miss
ratio — and links ozz-animation, sampling a two-joint linear clip at start,
midpoint, and end and checking it against the same linear expectation the
engine's own sampler produces. It also runs Recast/Detour: a plane with a wall
and a gap is voxelized into a navmesh and Detour finds a multi-polygon path
around the wall, so the navigation library is exercised rather than declared.
It also decodes the audio clip through miniaudio and opens a null playback
device, so the audio library runs headless, and it loads a system font through
FreeType and shapes a short string through HarfBuzz, so the text stack is
exercised too, it compresses the cooked package with zstd and checks the
byte-for-byte round trip, measuring the ratio on a real payload, it
reloads the package from disk and rebuilds a mesh from it to show the reload
lifecycle releases and rebuilds without accumulating scene objects, and it
links the Tracy client and marks frames, so profiling is compiled in. Fetch the
Tracy source with `python3 scripts/fetch_tracy.py`. The game's own navigation decisions stay in Elisa.

`examples/maze/game.elisa` also publishes its fog-of-war rule
(`maze_fog_radius`, `maze_cell_visible`), which both hosts render by
hiding geometry outside the player's radius.

`examples/maze/main.elisa` is the packaged headless entry point: it
starts, refuses input until playing, wins the scripted route, plays the
losing route to the last life, restarts, pauses and resumes, and exits,
reporting each step through its exit status.
`scripts/maze_game.elisascript` compiles and runs it.

`scripts/package_release.py` assembles a deterministic release for the
validated platform: it builds the packaged game, collects the cooked asset
package and the canonical fixture, writes a `release.json` pinning the engine
commit and every file hash, and produces a byte-deterministic `.tar.gz`. It
packages twice and fails if the archives differ, and the validation report only
records a release whose reproducibility was proved. Platforms that have not
been run are listed as untested rather than implied from a dependency's
platform list.

`examples/maze/` holds the first complete game as Elisa-owned rules: grid
topology with walls, hazards, a locked door plus key, goal, lives, fog-of-war
visibility, audio cues as data, menu flow, restart, and saved settings.
Backends render derived transforms and play cue IDs; they never decide
movement or win/loss.

Gameplay-adjacent ownership lives in small policy modules, each gated by
tests: `src/physics/policy.elisa` (one solver per body, kinematic from
Elisa, dynamic from the solver, tick-boundary commits),
`src/runtime/schedule.elisa` (declared read/write sets, conflict ordering,
automatic earliest-fit wave assignment feeding an auto-derived executor plan,
and a submitted-bytes counter the inspector budgets separately from frame time,
parallel pairs) plus `src/runtime/executor.elisa` (builds a flat-group
plan, validates it against the analysis, derives the serial order, and
dispatches each system through a single function value, recording the
order it actually ran; the serial reference a parallel executor must
match), `src/animation/state.elisa` plus `src/animation/codec.elisa`
(Elisa-owned clips, blends, events, root motion; benchmarked codec choice;
`examples/maze/hunter.elisa` drives the walking character's animation from
its movement, so one walked step is one Walk tick and arriving switches to
Idle),
`src/nav/grid.elisa` (BFS waypoints; the path is Elisa's decision),
`src/animation/ik.elisa` (two-bone IK and aim constraints, including exact
knee placement from the triangle projection without inverse trig) and
`src/animation/pose.elisa` (local-to-model pose evaluation, normalized-linear
blending, root-motion extraction, and a column-major skinning payload) and
`src/animation/skin.elisa` (linear blend skinning: a rest vertex is expressed
in each influencing joint's bind space, moved by that joint's model transform,
and blended by weight) and
`src/animation/sampler.elisa` (keyframe clips stored as flat parallel columns;
sampling interpolates a local transform at a tick and holds the end keys rather
than extrapolating).
`examples/maze/character.elisa` composes these into one playable enemy: it is
an owning World entity that navigates, animates, carries an IK-corrected leg
pose, and unloads with its identity removed first.
`examples/maze/pose.elisa` solves a two-bone leg in the maze plane with the
engine's IK, so the character's hip, knee, and foot are gameplay math; the
fixture carries those joints and both hosts consume them (Godot builds joint
markers, the native host validates them). The engine also skins a quad with
`src/animation/skin.elisa` and the fixture carries the deformed vertices, so
both hosts upload the skinned mesh rather than skinning it themselves.

`examples/maze/rootmotion.elisa` makes the sampled clip's root translation
drive movement: a cell is committed only when the accumulated root distance
reaches the cell length, so a shorter clip takes more cycles per cell instead
of a fixed timer deciding motion.
`src/assets/streaming.elisa` keeps a bounded residency of 2x2-cell chunks as
the player moves: chunks inside the radius load, chunks outside unload, and the
resident count never exceeds the budget because the farthest desired chunks are
evicted first. `examples/maze/game.elisa` drives it from the real player
position, so streaming follows gameplay and a restart leaves residency coherent.

The fixture carries the portable input mapping (`input_forward`, `input_left`,
`input_right`); the native host pushes and polls SDL key events and the Godot
probe parses synthetic key events, and both must resolve the bound keys to the
same portable button names the engine's input map uses, never to enum ordinals.

Live input can also drive gameplay directly: `examples/maze/capi.elisa` exports
the game as a C ABI with the compiler's `-emit c-archive`, and
`native/embed_probe.cpp` links that archive, reads an SDL key event, maps it to
a move code, and calls `maze_step` in-process; it also queries the world and
plays the winning route through the same exports. `python3 scripts/embed_probe.py`
builds and runs it. So a host can feed input while Elisa still owns identity,
rules, and state (ADR-0012).

The Godot host reaches the same gameplay without a binding release:
`scripts/fetch_gdextension_header.py` dumps the installed Godot's own
`gdextension_interface.h`, and `backends/godot-embed/elisa_godot_bridge.cpp`
binds an abstract, static `ElisaMaze` class straight to the `libmaze` exports —
the session is process-global, so the class offers class-level calls rather
than pretending every object owns state. `python3 scripts/godot_embed_probe.py`
emits the archive, builds the extension, imports it, and plays a full session
(start, movement, hazard, reset, win) that must agree with the native
embedding move for move (ADR-0013).

The fixture also carries the engine's UI theme (`menu_style_*`), so a host
applies Elisa's surface and label colors instead of choosing its own:
`src/ui/style.elisa` owns the theme and `test/editor.elisa` pins its state
rules. The Godot probe builds real `StyleBoxFlat` surfaces and verifies the
focused and disabled label colors; the native probe applies the same values
to Wicked widgets before they are removed.

The fixture also carries the menu state (`menu_actions`, `menu_enabled`,
`menu_focus`); `backends/godot/probe.gd` consumes it with real Godot controls
and the gated probe verifies the focused row and the disabled rows, so UI data
reaches a host's UI system rather than staying in the model. The native host
builds the same menu as real Wicked GUI buttons, verifies their enabled states,
and removes them before capture, so both backend families render Elisa UI
widgets from the same data.

`src/ui/text.elisa` breaks text into lines: a host shapes with a font stack and
hands back word widths, and the engine packs words greedily (a word wider than
the line takes one line, so overflow is recorded rather than hidden).

`src/ui/layout.elisa` is the engine-owned layout engine: boxes with an
orientation, padding, and spacing whose rectangles are computed in one forward
pass, used for panels and rows while a host paints the rectangles.

`src/ui/menu.elisa` is the engine-owned menu model: a fixed-height vertical
list with focus navigation that skips disabled rows, a visible window that
scrolls to keep the focused row on screen, pointer hit testing that accounts for
the scroll offset, and activation returning an action id the game maps to its
own flow. No text and no
rendering live in the model; `examples/maze/menu.elisa` decides which actions
each game state offers, so the model never learns the game's rules.

`src/runtime/save.elisa` stores portable save blobs as a magic marker, a
format version, a field count, and plain i64 fields, rejecting a blob whose
magic, version, or length disagrees. `examples/maze/persist.elisa` saves and
restores the game's settings, status, player cell, moves, lives, and key, and
re-derives stream residency on load; a wrong version or a corrupt marker is
refused.

`examples/maze/studio.elisa` is the play-in-editor surface: a running game with
an undo/redo history over its settings, buffered input that a host feeds and the
studio drains one action per advance, stepped through the real game API, and an
asset-catalogue check that rejects a stale package generation.
`src/tooling/reload.elisa` demonstrates the three code-reload prerequisites
the plan names before loading code: a quiescence gate that is open only when no
system is mid-step and no callback is in flight, and explicit version-to-version
state migration that refuses an unknown or downgraded target instead of
guessing. Loading code itself stays outside the engine.
`src/math/geometry.elisa` supplies a Newton square root, lengths,
normalization, quaternion multiply/rotate, and transform composition, so
rotation stays engine-owned instead of a vendor type.
`src/audio/policy.elisa` (one device, playback first, spatial opt-in),
`src/tooling/inspector.elisa` plus `src/tooling/editor.elisa` (read-only
snapshots, perf budgets, undo/redo, reload generations), and
`src/net/replication.elisa` plus `src/net/session.elisa` (authority,
interpolation data, deterministic loss profile, rollback, sessions with
recovery), and `src/net/wire.elisa`, `src/net/loopback.elisa`,
`src/net/peer.elisa`, `src/net/prediction.elisa`,
`src/net/interpolation.elisa`, `src/net/recovery.elisa`, and
`src/net/reliable.elisa` (a fixed little-endian
replication frame with an exact encode/decode round trip; an in-memory loopback
link applying the declared loss and delay policy; a peer that accepts only
authorized, newer writes and converges on the server's final state over that
lossy link; client-side prediction that applies input immediately, replays
unacknowledged inputs over each authority snapshot, and counts corrections; and
interpolation that renders between the two most recent authority snapshots and
clamps rather than extrapolating; and recovery that detects a revision gap,
blocks application until a snapshot resyncs, and never rewinds the rollback
high-water mark; and a sliding window that resends the oldest unacknowledged
record so a dropped frame is recovered rather than waited out). The loopback is a transport double: it proves the
encode/deliver/decode/reconcile chain under loss and latency, not that a
real socket works. None of these link their native libraries yet; they
establish the contracts those integrations must satisfy.

The current stage1 compiler rejects direct copies of the affine allocator,
World, and scene recorder from borrowed parameters; the check script verifies those rejection
diagnostics. The allocator field and registry storage are still publicly
representable, so arbitrary Elisa callers can construct or mutate invalid World
values. Full encapsulation and ownership enforcement remain open. World proofs
cover the pure epoch predicate, not mutable registry consistency.

### Checks

Requires an ElisaScript launcher, the self-hosted Elisa compiler, a built
Elisa Proof assistant, and SDL3 for the platform probe (set
`ELISA_SDL3_LIB_DIR` when SDL3 is installed outside `/opt/homebrew/lib`). The
native probe additionally needs the Homebrew `freetype` and `harfbuzz` libraries
for the text check:

```sh
PATH="$HOME/.elisac:$PATH" elisascript scripts/check.elisascript
```

For other installations, set `ELISA_COMPILER_BIN` and `ELISA_PROOF_BIN` to executable
paths. By default the prover is read from the sibling `elisa-proof/build/elisa-proof`.
The script passes tool arguments directly without a shell and returns nonzero on
compilation, runtime test, proof, or provenance-report failure. It saves the proof
reports in `build/entity-id-proof.json` and `build/world-proof.json`, then writes
`build/validation.json` only after both JSON results report a proved state, zero
diagnostics, complete certificate replay, and independent kernel replay. That
validation record includes SHA-256 identities for the engine source manifest,
compiler entry/product, prover, and ElisaScript launcher. It does not rebuild
either toolchain. The same record enforces the 600-line source-file limit
(`source_length_policy`) and records a reproducible release archive
(`release`), so a release is only reported when the packager proved its two
archives byte-identical.

- `test/`: executable checks for identity edges, geometry and asset values, input mapping, backend capability selection, SDL3 platform initialization, Godot host command ordering, fake bridge retention/callback lifecycles, FFI ownership contracts, recording command order, fixed stepping, scripted headless gameplay, canonical scene bridging, tolerance image comparison, asset cooking and catalogue generations, maze topology and the complete maze game (win/loss, door/key, hazards, fog, settings), animation state and codec choice, grid navigation, inspector snapshots and perf counters, replication scope and net sessions, physics authority, scheduler ordering, editor undo/reload, and the shippable bundle manifest, plus World lifecycle, including
  deterministic churn, compaction, capacity rejection, and corrupted-store detection;
  negative compilation fixtures reject direct copies of all three affine owners.
- `proof/`: Elisa Proof checks importing the actual implementation. They establish
  that a valid cursor advances by one and produces an ID greater than every earlier
  ID bounded by that cursor. Imported runtime code is checked as well.

These proofs do not yet establish whole-world ownership, liveness, concurrency safety,
or backend correctness. Runtime tests cover exhaustion, state preservation, and
registry/store consistency; the proof contracts do not claim those mutable-state
postconditions.

## Next milestone

A screenshot-driven pixel comparison using the declared tolerance, native
solver linkage behind the physics authority policy, and an editor surface
over the inspector/undo/reload foundation. `scripts/check.elisascript`
stands at 593 of the 600-line file limit and must be split before further
suites land. The project license is still explicitly undecided and blocks
any distribution.

### ElisaScript migration status

The shell check has been replaced by `scripts/check.elisascript`. ElisaScript’s
existing process, environment, filesystem, and `Script::source_path()` APIs
cover the workflow; no new process API is needed. The script locates the engine
relative to its own source path, so invocation does not depend on the working
directory. Empty tool overrides use the defaults.

Validated on 2026-09-15 with the native ElisaScript launcher: entity runtime tests
passed, all 15 proof obligations were proved and replayed, and ten comparison cases
matched the original shell workflow. The launcher is installed at
`~/.local/bin/elisascript` on this workstation.
On 2026-09-17 the extended check also passed the World lifecycle binary,
verified affine ownership rejection diagnostics, replayed both World
epoch-predicate proof obligations, and wrote the hashed validation record. The
fake-tool comparison now covers 15 cases.

On 2026-09-18 the gate grew to 31 runtime suites plus proofs: canonical
scene bridge, tolerance image comparison, asset cooking and catalogue,
maze topology and the complete game, animation state and codec choice,
grid navigation, inspector and perf counters, replication scope and net
sessions, physics authority, scheduler ordering, editor undo/reload, and
the shippable bundle manifest. The compiler snapshot moved from a950b5cd
to 76230afa after the older snapshot declined `catch` over `void`; the
minimized repro is documented in the sibling compiler history.

ElisaScript and its native compiler required integration fixes. Build identities,
regressions, and scope are recorded in the sibling
[ElisaScript validation notes](../elisa-script/docs/engine-check-validation.md).
The script relays captured stdout/stderr after each tool exits.

To rerun the comparison with fake tools:

```sh
python3 test/check_workflow.py "$HOME/.local/bin/elisascript"
```

### Tail-return support

The allocator uses implicit function tail returns. The installed compiler and
prover were refreshed and the runtime/proof check passes. Scope, regression
evidence, and rebuild provenance are in
[tail-return validation](docs/tail-return-validation.md).
