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

`WorldSchedule::Frame` is the runtime owner for portable event dispatch. It
aligns event phases with world access phases, dispatches under a read token,
and validates each delivered entity reference against the live World while
reading the event. Structural changes remain deferred until that token has
been released. The environmental-effects example and native effect smoke now
consume Spawn payloads through this schedule API; the consumer cannot receive
an event from a stale or replaced World.

The native worker bridge remains a separately tested boundary. Its events are
not yet drained into `WorldSchedule::Frame`, so worker-produced game payloads
do not reach Elisa runtime consumers through one shared phase owner yet.

`test/world_events.elisa` covers phase regression, subscription, delivery,
unsubscription, stale-phase emission, and rejection of both despawned and
wrong-epoch entity references.
