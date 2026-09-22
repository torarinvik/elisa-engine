# World event phase validation

`src/world/events.elisa` provides a bounded typed queue with explicit service
phases: input, simulation, physics, animation, render, and audio. Producers can
emit only during the active frame and cannot emit into a phase that has already
passed. Listener IDs are subscribed to one phase, can be removed before
delivery, and listener zero is reserved for broadcast events.

Dispatch marks each event consumed, so repeated drains do not deliver it twice.
`Event` carries a branded `World::EntityRef`. `emit_world` checks that reference
against the primary `World` before enqueueing, so despawned entities and
references from an older world epoch are rejected. The lower-level `emit`
remains available for events whose entity identity is resolved by another
service, but still requires a positive epoch and ID.
The queue has no callback invocation; consumers drain a phase and then mutate
the world under that phase's access rules. This makes reentrant callbacks and
worker-to-main handoff explicit integration work rather than hidden mutation.

`test/world_events.elisa` covers phase regression, subscription, delivery,
unsubscription, stale-phase emission, and rejection of both despawned and
wrong-epoch entity references.
