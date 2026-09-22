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
reject structural mutation, and a frame cannot advance or end while a token is
live. `src/world/phase_commands.elisa` acquires that structural token around a
primary-world commit, and the phase section of `test/world_commands.elisa`
covers the phase boundary, conflict, and release rules.

Focused validation on macOS 27.0 / Apple M5:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
/Users/torarinvikbjarko/.elisac/elisac-stage1 -emit exe \
    -o build/world-command-primary-test test/world_command_primary.elisa
./build/world-command-primary-test
```

The command exited successfully. Compiler phase-borrow lifetime diagnostics and
event scheduling integration remain W03 follow-up work.
