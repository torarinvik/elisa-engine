# Native resource handle validation

`native/resource_handles.h` keeps a vendor entity behind a slot, generation,
and scene-owner identity. Logical destruction invalidates the handle
immediately. Deferred destruction records the submission serial and only
removes the Wicked entity after the caller reports a completed serial, so a
resource referenced by in-flight GPU work remains alive until its fence.

`native/resource_handle_probe.h` exercises creation, stale-handle rejection,
cross-scene rejection, slot protection while retired, serial 6 versus serial 7
collection, generation reuse, and ordinary destruction in the real native
scene. The registry is still an entity-component adapter; separate material,
texture, physics-body, and voice pools remain follow-up F06 work.

