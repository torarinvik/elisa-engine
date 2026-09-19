# Native resource handle validation

`native/resource_handles.h` keeps mesh, material, texture, and physics-body
resources behind a kind, slot, generation, and scene-owner identity. A real
1x1 Wicked GPU texture is held in the texture pool; entity kinds resolve only
to scene entities. Logical destruction invalidates every handle immediately.
Deferred destruction records the submission serial and only releases the
entity or texture after the caller reports a completed serial.

`native/voice_handles.h` adds the same owner/generation contract over the real
bounded miniaudio voice service. The native probe exercises all four scene
families and voices, stale/cross-scene rejection, serial 2 versus serial 3
collection, generation reuse, failed generation-exhaustion creation, and
shutdown invalidation. Test-only generation injection keeps exhaustion
deterministic without widening the public ABI.
