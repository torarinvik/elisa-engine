# Render resource validation

`src/backend/render_resources.elisa` keeps mesh and material asset IDs shared
between instances and stores only transform, visibility, and layer state per
instance. Generation-checked handles make destruction local to one instance;
another instance can keep using the same mesh and material IDs.

The bounded registry exposes read/update/destroy operations for a native Wicked
adapter and counts references to shared meshes without transferring ownership
of the underlying asset. `test/render_resources.elisa` covers two instances,
independent transform updates, shared-reference counts, destruction, and stale
handle rejection. Device uploads and material ABI encoding remain native R02
work.

The native Wicked gate also applies material color, visibility, and layer-mask
updates through `NativeResourceRegistry` handles before destroying the
resources, proving the instance update path stays behind the owner-checked
native boundary. Elisa and native adapters now accept bounded transactional
instance-update batches: every handle and component is checked before any row
is changed, duplicate handles are rejected, and telemetry records batch calls,
rows, and rejected submissions as an ABI traffic measure. The shared and
native gates cover two independent rows plus duplicate-batch rejection.
