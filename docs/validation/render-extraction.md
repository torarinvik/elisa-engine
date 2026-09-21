# Render extraction validation

`src/backend/render_snapshot.elisa` is the persistent frame-facing extraction
layer between gameplay and native hosts. It owns bounded rows keyed by a
world-branded gameplay reference and a stable render ID, allowing one gameplay
entity to produce multiple render instances without exposing vendor entities as
game identities. Updates replace a row's transform; removal compacts the table.

`test/render_snapshot.elisa` and the shared ElisaScript gate cover one-to-many
fan-out, update, removal, capacity bounds, and invalid-reference rejection.
A render ID belongs to one gameplay identity at a time: `snapshot_bind` rejects
a second entity claiming a bound render ID, and `snapshot_valid` checks every
row. Until 2026-09-21 `snapshot_valid` discarded its row loop; see
[`validator-loops.md`](validator-loops.md).
The Wicked probe also exercises `native/render_snapshot_bridge.h`: it creates
two native rows for one logical fanout, updates one independently, rejects an
unknown removal, and returns the scene object count to its baseline. Vendor
entities remain separate from gameplay references.

The native bridge also accepts a bounded batch. It validates every render ID,
position, and scale before creating entities or changing transforms, rejects
duplicate IDs, and rolls back newly created entities when staging fails. The
probe covers a successful two-row fanout batch and an invalid batch that leaves
the native object count and live-row set unchanged.
