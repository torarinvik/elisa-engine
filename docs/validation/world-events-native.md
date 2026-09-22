# Native world-event delivery

`native/world_event_bridge.h` is the native half of the portable
`WorldEvents::Queue` contract. It keeps event storage bounded and lets worker
threads emit into a mutex-protected queue while the main thread advances phases
and performs callbacks.

## Contract

- A frame starts in `Input` and may advance only one phase at a time through
  `Simulation`, `Physics`, `Animation`, `Render`, and `Audio`.
- Producers can emit only live, subscribed listener IDs with positive entity
  IDs. Events from an earlier phase, unknown listeners, inactive frames, and a
  full queue are rejected.
- Delivery takes a fixed snapshot under the queue lock, releases the lock, and
  invokes callbacks one at a time. The bridge rejects reentrant dispatch and
  phase advancement while a callback is active. If a callback unsubscribes a
  listener, later events for that listener in the snapshot are consumed without
  invocation.
- Unsubscribing a listener immediately suppresses queued events for that
  listener. `end_frame` closes the producer boundary and reports the delivered
  count.

## Evidence

The Wicked native gate emits a simulation damage event from a worker thread,
rejects an out-of-order phase transition, delivers it once on the main thread,
rejects a second delivery, blocks recursive dispatch and phase advancement from
the callback, suppresses a later queued event after listener destruction, and
rejects an invalid close transition.

The portable queue remains the source of truth for typed Elisa events; this
adapter supplies the native thread handoff and callback boundary.
