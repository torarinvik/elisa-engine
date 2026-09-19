# Render extraction validation

`src/backend/render_snapshot.elisa` is the persistent frame-facing extraction
layer between gameplay and native hosts. It owns bounded rows keyed by a
world-branded gameplay reference and a stable render ID, allowing one gameplay
entity to produce multiple render instances without exposing vendor entities as
game identities. Updates replace a row's transform; removal compacts the table.

`test/render_snapshot.elisa` and the shared ElisaScript gate cover one-to-many
fan-out, update, removal, capacity bounds, and invalid-reference rejection.
The Wicked probe also exercises `native/render_snapshot_bridge.h`: it creates
two native rows for one logical fanout, updates one independently, rejects an
unknown removal, and returns the scene object count to its baseline. Vendor
entities remain separate from gameplay references.
