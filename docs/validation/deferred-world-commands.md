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
schedule read token and records the world epoch and live-column length; the
token stays held until `close_cursor`. While a cursor is open the frame cannot
advance, end, or commit structural commands, so an iteration borrow cannot
outlive its phase and a deferred despawn cannot interleave with it. A cursor
that observes a world changed behind the schedule (different epoch or live
length) raises `WorldChanged` instead of yielding a reference. Closing against
another frame is rejected and leaves the owning frame locked; a closed cursor
cannot be read or closed again. The step rule lives in
`src/world/iteration_bounds.elisa` and is proved by
`proof/world_iteration_bounds.elisa` (reads only below the recorded length,
each step stays within it).

`test/world_phase_iteration.elisa` runs in the gate's unit-test list. Negative
controls: dropping the live-length check fails with 27 (a reference is yielded
after a bypassing spawn); closing without releasing the token fails with 21
(the deferred despawn stays blocked). Weakening the step proof's ensure fails
the prover.

Stage1 does not enforce affinity for affine structs with only scalar fields: a
probe copying a `WorldAccess::Token` (or a cursor) from a reference, and a
use-after-move of a local, both compile. A copied token can therefore be
released twice; the next slice makes release reject unknown token serials.
