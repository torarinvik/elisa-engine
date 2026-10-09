# Deferred world command validation

`src/world/commands.elisa` collects spawn, despawn, and reparent requests in a
bounded affine buffer. Commands carry a deterministic insertion order; a batch
with duplicate structural writes for one identity is rejected as a whole, and
`commit_with_capacity` clears only a valid batch whose spawn requests fit the
executor's free-slot budget. A capacity failure preserves every command, so
allocation failure and conflicting events remain visible before application.

`test/world_commands.elisa` is part of the shared ElisaScript gate and covers a
valid mixed batch, duplicate rejection, capacity rejection with retention, and
the commit boundary. `test/world_command_primary.elisa` exercises the primary
registry path: accepted spawn/despawn/reparent commands clear the affine buffer,
invalid references, cycles, and duplicate writes remain pending, and a mixed
spawn plus invalid reparent leaves the live count unchanged. The world
preflight rejects invalid commands before application; an unexpected apply
failure restores the captured `World::Rollback::Snapshot`.

`src/world/access.elisa` adds an affine six-phase access frame. Read and write
tokens conflict as expected, structural tokens are exclusive, input and render
reject structural mutation, and a frame cannot restart, advance, or end while a
token is live. `src/world/events.elisa` likewise rejects a second start without
clearing queued events. `src/world/phase_commands.elisa` acquires that structural token around a
primary-world commit, and the phase section of `test/world_commands.elisa`
covers the phase boundary, conflict, and release rules.

Each access frame receives a private process-wide identity on its first start,
and each token keeps that owner identity. Releasing a live token against another
frame returns `WrongFrame` without changing either frame's access state. The
regression starts two frames, tries to release one frame's read token against a
write-locked second frame, then verifies that the original token remains live
and the second frame still rejects another writer. The identity counter is
currently owner-thread-only; W07 must provide atomic identity issuance before
frames can start concurrently.

Focused validation on macOS 27.0 / Apple M5:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/Users/torarinvikbjarko/.elisac/elisac-stage1 -emit exe \
    -o build/world-command-primary-test test/world_command_primary.elisa
./build/world-command-primary-test
```

The focused command exited successfully. The full shared gate also passed on
2026-09-27 with the current Stage1 compiler and its runtime object:

```sh
WICKED_ROOT="$PWD/../amazing-labyrinth-wickedengine" \
WICKED_BUILD="$PWD/../amazing-labyrinth-wickedengine/build-elisa-sdl3" \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/runtime/elisacore_runtime.o" \
GODOT_BIN=/opt/homebrew/bin/godot \
PYTHON_BIN=/opt/homebrew/bin/python3.14 \
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/Users/torarinvikbjarko/.local/bin/elisascript scripts/check.elisascript
```

Compiler phase-borrow lifetime diagnostics remain W03 follow-up work.

## 2026-09-30: primary-world test repaired and gated

`test/world_command_primary.elisa` was not in the shared gate and had been
failing at HEAD with code 9. The static-spawn case asserted that the commit
*fails* and then required three live entities, which contradict each other.
Once commit 08e274af fixed the catch arms, the spawn committed and the stale
assertion tripped. The test now requires the static spawn to commit, and
failure codes that were used twice (4, 7–9, 11–13) are renumbered 27–33 so each
failure is unique. It runs in the gate's unit-test list. Negative control:
skipping `world_batch_valid` in `commit_world` fails with 14 (a duplicate
despawn is applied).

## 2026-10-01: phase-bound world iteration cursor

`src/world/phase_iteration.elisa` adds `WorldPhaseIteration::Cursor`, the
supported way to walk live entities during a phase. Opening a cursor acquires a
schedule read token and records the world epoch, structural revision, and
live-column length; the token stays held until `close_cursor`. The revision
advances on spawn, despawn, reparent, and rollback restore. This detects a
despawn followed by a spawn even when the live count and world epoch are
unchanged. While a cursor is open the frame cannot
advance, end, or commit structural commands, so an iteration borrow cannot
outlive its phase and a deferred despawn cannot interleave with it. A cursor
that observes a world changed behind the schedule (different epoch, structural
revision, or live length) raises `WorldChanged` instead of yielding a reference. Closing against
another frame is rejected and leaves the owning frame locked; a closed cursor
cannot be read or closed again. The step rule lives in
`src/world/iteration_bounds.elisa` and is proved by
`proof/world_iteration_bounds.elisa` (reads only below the recorded length,
each step stays within it).

`test/world_phase_iteration.elisa` runs in the gate's unit-test list. It
bypasses the scheduler with a despawn and replacement spawn, verifies that the
world epoch and live count stay unchanged while the structural revision moves,
then requires the cursor to reject the stale traversal. Negative controls:
dropping the revision check fails with 27; closing without releasing the token
fails with 21 (the deferred despawn stays blocked). Weakening the step proof's
ensure fails the prover. `test/world.elisa` also verifies that restoring a
rollback snapshot advances the structural revision.

Stage1 does not enforce affinity for affine structs with only scalar fields: a
probe copying a `WorldAccess::Token` (or a cursor) from a reference, and a
use-after-move of a local, both compile. A copied token can therefore be
released twice; the next slice makes release reject unknown token serials.

## 2026-10-01: serial-checked token release

Each access frame now keeps a bounded table of live token serials
(`WorldAccessBounds::MAX_LIVE_TOKENS`, 16). Acquire refuses with
`TooManyBorrows` when the table is full and stamps the token with a fresh
serial; release must find and clear that serial or fails with `UnknownToken`.
A copied token (which Stage1 currently accepts) can therefore release at most
once: after the original releases, the copy is rejected and the frame's reader
count and remaining holders are untouched, so another reader's borrow cannot be
dropped early and a structural commit cannot slip in under it.
`test/world_access_serials.elisa` copies a read token, releases the original,
shows the copy is refused while a second reader still blocks structural access
and phase advance, then fills all 16 slots and checks the 17th is refused.
Negative control: removing the serial lookup in `release` fails with 7.
`proof/world_access_bounds.elisa` proves the slot bound and that the
"none" sentinel is never usable; weakening it fails the prover.

What W03 still lacks is compile-time: Stage1 neither rejects copies of
scalar-only affine structs nor tracks phase-borrow lifetimes. The runtime frame
now fails closed on both (copies cannot double-release; a leaked token keeps
the phase locked), but the Done wording about borrows is enforced dynamically,
not by the compiler.

The full `scripts/check.elisascript` gate passed on 2026-10-01 with both slices
(ending "Validation report written.").

## 2026-10-03: detect equal-count structural churn

The cursor also records `World::world_structure_revision`. Spawn, despawn,
reparent, and rollback restore advance it, so an out-of-schedule despawn plus
replacement spawn is rejected even though the world epoch and live count are
unchanged. A bounded exhaustion error prevents the revision from wrapping;
world command commits reserve one additional revision for atomic rollback.
`test/world_phase_iteration.elisa` covers the equal-count bypass, and
`test/world.elisa` checks that rollback restore advances the revision.

Focused compile-and-run checks passed for `world`, `world_phase_iteration`,
`world_commands`, `world_command_primary`, and `world_events`. The full
`scripts/check.elisascript` gate then passed on macOS 27.0.1 / Apple M5: 211
tests, SDL3 platform checks, Godot probes, native unit tests, and 67 proofs.
The recorded compiler product is Stage1 commit `b903bd1e` (SHA-256
`734fad7984b0c6de3b50e6d57585e3c8573f975c33e4560f647a1209f9ed828d`); prover
commit `6d6b6652` (SHA-256
`827f274506a9aa4b2d43b36728ad48a853cdfe232ce25b20202a04239e5d4982`). The
validation report is `build/validation.json`.

## 2026-10-09: aggregate spawn-capacity preflight

`WorldCommands::world_batch_valid` now counts all requested spawns and checks them
against the same remaining world capacity before it captures a rollback snapshot.
The previous per-command check compared every spawn with the unchanged pre-commit
live count. A two-spawn batch against a world with one free slot therefore entered
application, added the first entity, then refused the second. The negative control
that removes the aggregate check exits at test assertion 40 with 256 live entities
after the rejected batch, demonstrating the partial mutation. The fixed path rejects
the batch before capture; the two commands remain pending, the live count and
allocator high-water mark stay unchanged, the structure revision does not advance,
and `World::world_is_valid` remains true.

`src/world/command_bounds.elisa` contains the pure budget predicate, and
`proof/world_command_bounds.elisa` proves exact-fit admission, over-budget refusal,
and refusal when the live count already exceeds the capacity. The new near-capacity
case is in `test/world_command_primary.elisa`.

Validation on macOS 27.0.1 / Apple M5, using the installed Stage1 snapshot from
compiler source `b11e9121` (source tree SHA-256
`40b306621d1e1aa7cdd68a73179360ba1b6b9ba61aae2d491cd3087d4b819fff`), product
SHA-256 `1505c598a71e76d0d7f1a201cdf458320960d9f024531eca2c4bff19f5c08c24`,
and matching runtime SHA-256
`013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`:

```sh
~/.elisac/elisac-stage1 -emit exe -o build/world-w03-exact-test test/world.elisa
build/world-w03-exact-test
~/.elisac/elisac-stage1 -emit exe -o build/world_commands-w03-exact-test test/world_commands.elisa
build/world_commands-w03-exact-test
~/.elisac/elisac-stage1 -emit exe -o build/world_command_primary-w03-exact-test test/world_command_primary.elisa
build/world_command_primary-w03-exact-test
~/.elisac/elisac-stage1 -emit exe -o build/world_phase_iteration-w03-exact-test test/world_phase_iteration.elisa
build/world_phase_iteration-w03-exact-test
../elisa-engine-proof/build/elisa-proof --json proof/world_command_bounds.elisa
```

All four executables exited 0. The negative control exits at the diagnostic
variant's 256-live-entity assertion when aggregate preflight is removed. The proof product from
`elisa-engine-proof@f593c886` (SHA-256
`d08b69e0b6fed0f6006351c862defcf5b57054e718300105fffb195f004c7d6f`) proved all
16 obligations. The negative control described above fails as expected. The full
shared gate was not rerun for this slice. W03 remains open for compiler-enforced
affine-copy and phase-borrow lifetime diagnostics.

## 2026-10-09: ordered primary-world preflight and rollback capture repair

An ordered probe found that a batch could despawn a parent and then reparent its
child to that now-missing parent. Per-command checks used the unchanged original
world, so the batch passed preflight; the later apply failed after the despawn.
The rollback path also exposed a capture-arm bug: successful capture constructed
a zeroed `Snapshot` instead of binding the returned snapshot. The `value: value`
arm now preserves the captured world.

`WorldCommands::world_batch_order_valid` copies only the bounded hierarchy into a
shadow tree when a batch contains a reparent. It replays despawns and reparents in
insertion order, including hierarchy updates, and rejects a missing child or
parent and cycles created by earlier commands. Reference checks still use the
primary world for epoch and liveness. This accepts a valid sequence that first
moves a child to root and then places its former parent under that child. The
preflight also checks the allocator's remaining ID range for all requested
spawns before application.

`test/world_command_primary.elisa` covers parent-despawn followed by child
reparent rejection with no world or revision change, a cycle formed by two
ordered reparent commands, and the valid order-sensitive reparent sequence.
It also forces an apply-time hierarchy overflow by composing two valid `3e38`
translations. That path verifies the captured snapshot restores both live
entities and leaves the rejected command pending; the structure revision advances
so any open iteration cursor is invalidated.
Focused checks passed on the installed Stage1 compiler:

```sh
~/.elisac/elisac-stage1 -emit exe -O0 -o build/world-test test/world.elisa
build/world-test
~/.elisac/elisac-stage1 -emit exe -o build/world-commands-test test/world_commands.elisa
build/world-commands-test
~/.elisac/elisac-stage1 -emit exe -o build/world_command_primary-test test/world_command_primary.elisa
build/world_command_primary-test
~/.elisac/elisac-stage1 -emit exe -o build/world-phase-iteration-test test/world_phase_iteration.elisa
build/world-phase-iteration-test
python3 scripts/check_source_length.py
```

All four executables exited 0; source-length policy and `git diff --check` pass.
The full shared gate was not rerun. W03 remains open for compiler-enforced
affine-copy and phase-borrow lifetime diagnostics.
