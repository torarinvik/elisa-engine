# Elisa Engine

An experimental Elisa-native game engine built around algebraic entity hierarchies,
numeric identity, and specialist engine libraries. SDL3 and headless Godot and
Wicked host probes are now exercised; the full Godot GDExtension and Wicked
renderer remain planned. [Native backend validation](docs/native-backend-validation.md)
records the pinned external Wicked checkout and its macOS build notes.

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
backend handles. It validates source descriptors and portable visual references;
it does not yet import, cook, or load asset bytes. `src/runtime/input.elisa`
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

The current stage1 compiler rejects direct copies of the affine allocator,
World, and scene recorder from borrowed parameters; the check script verifies those rejection
diagnostics. The allocator field and registry storage are still publicly
representable, so arbitrary Elisa callers can construct or mutate invalid World
values. Full encapsulation and ownership enforcement remain open. World proofs
cover the pure epoch predicate, not mutable registry consistency.

### Checks

Requires an ElisaScript launcher, the self-hosted Elisa compiler, a built
Elisa Proof assistant, and SDL3 for the platform probe (set
`ELISA_SDL3_LIB_DIR` when SDL3 is installed outside `/opt/homebrew/lib`):

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
either toolchain.

- `test/`: executable checks for identity edges, geometry and asset values, input mapping, backend capability selection, SDL3 platform initialization, Godot host command ordering, fake bridge retention/callback lifecycles, FFI ownership contracts, recording command order, fixed stepping, scripted headless gameplay, and World lifecycle, including
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

Complete allocator encapsulation and the remaining World ownership checks. Add
asset loading/cooking and service ownership contracts before connecting either
renderer.

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
