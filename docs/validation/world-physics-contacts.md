# Checked physics contact identity — 2026-10-07

Jolt contact records contain native participant keys. Those numbers cannot be
cast to World entity IDs. `PhysicsRuntime::body_contact_key` and its service
variant query a body through its checked world/slot/generation handle;
`RuntimeServices::physics_body_contact_key` adds the session checks. The C ABI
checks owner/world state and body generation before writing the key.

`WorldPhysics::body_contact_key` queries an existing binding without exposing
its native handle. `WorldPhysics::contact_reference` joins a participant key
against the binding owner and verifies the resulting reference is live in the
supplied World. The World epoch and entity generation remain part of that
check. Unknown/unbound participants return `{world_epoch: 0, id: 0}`. Stale
physics handles and invalid matching World references remain explicit errors.
Keys are runtime identities for joining contact records, not serialized IDs.

## Native evidence

`world-physics-pose-smoke` passed on SDL3/Jolt/Metal with silent audio fallback,
status 0, 52.021 seconds including compile/link. It verifies:

- A current body's nonzero key resolves to its exact World reference.
- Zero and an unbound old key produce the invalid reference.
- A different World with the same numeric entity ID is rejected by epoch.
- A despawned World entity is rejected while its body is still bound.
- Unbinding and creating a replacement body yields a different key; the old
  key stays unresolved even when the binding table is nonempty again.
- Restarting the physics session rejects the previous binding as `StaleWorld`.

Existing pose, hierarchy, kinematic and cleanup assertions also passed, and
its hierarchy capture changed across 22860 pixels at 640×400. Results are in
`build/native-smoke/world-physics-pose-smoke.json` and its log. These are native
outcome assertions; they do not constitute a whole-program formal proof.

## Remaining work

Consume copied Jolt contacts after each fixed step through `WorldSchedule`,
resolve their participants with this lookup, and deliver one crate impact cue
under the Audio phase. Phase/access rejection, bounded overflow accounting,
unsubscribe behavior and the actual course consumer remain acceptance work.
The existing native contact queue remains the worker handoff mechanism.
