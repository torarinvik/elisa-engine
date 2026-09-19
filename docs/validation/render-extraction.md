# Render extraction validation

`src/backend/render_snapshot.elisa` is the persistent frame-facing extraction
layer between gameplay and native hosts. It owns bounded rows keyed by a
world-branded gameplay reference and a stable render ID, allowing one gameplay
entity to produce multiple render instances without exposing vendor entities as
game identities. Updates replace a row's transform; removal compacts the table.

`test/render_snapshot.elisa` and the shared ElisaScript gate cover one-to-many
fan-out, update, removal, capacity bounds, and invalid-reference rejection.
Native Wicked submission remains a later adapter over this snapshot contract.
