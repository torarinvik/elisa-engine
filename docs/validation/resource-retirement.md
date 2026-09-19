# Native handle retirement validation

`NativeResourceRegistry` now separates logical destruction from physical scene
removal. A `destroy_deferred` call invalidates the generation immediately and
protects the slot while the real Wicked cube entity remains in a retirement
queue. `collect_retired` removes the entity after the caller's GPU completion
point, then permits slot reuse with a new generation.

The native Wicked gate exercises this against real scene cubes: stale and
cross-scene handles are rejected, the retired entity remains resolvable only by
the registry's internal queue, slot reuse is blocked until collection, and the
reclaimed slot receives a different generation. The queue is ready for the
graphics device's fence callback; mesh/material/texture/body/voice registries
are still separate F06 follow-up work.
