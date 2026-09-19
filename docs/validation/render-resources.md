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
