# Deferred world command validation

`src/world/commands.elisa` collects spawn, despawn, and reparent requests in a
bounded affine buffer. Commands carry a deterministic insertion order; a batch
with duplicate structural writes for one identity is rejected as a whole, and
`commit_with_capacity` clears only a valid batch whose spawn requests fit the
executor's free-slot budget. A capacity failure preserves every command, so
allocation failure and conflicting events remain visible before application.

`test/world_commands.elisa` is part of the shared ElisaScript gate and covers a
valid mixed batch, duplicate rejection, capacity rejection with retention, and
the commit boundary. Applying accepted commands to the primary `World` registry
remains W03 follow-up.
