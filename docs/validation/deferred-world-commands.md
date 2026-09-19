# Deferred world command validation

`src/world/commands.elisa` collects spawn, despawn, and reparent requests in a
bounded affine buffer. Commands carry a deterministic insertion order; a batch
with duplicate structural writes for one identity is rejected as a whole, and
`commit` clears only a valid batch. This keeps iteration phases independent of
structural mutation and makes failed allocation or conflicting events visible
before application.

`test/world_commands.elisa` is part of the shared ElisaScript gate and covers a
valid mixed batch plus atomic rejection and retention of a conflicting batch.
Applying accepted commands to the primary `World` registry remains W03 follow-up.
