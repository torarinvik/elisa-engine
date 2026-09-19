# Save schema validation

`src/world/save_schema.elisa` stores stable gameplay IDs and plain `i64` fields;
native handles are not part of the durable record. Records carry a schema
version and deterministic checksum. Version 1 records migrate to version 2 by
adding the new field with its defined default and recomputing the checksum.

Writes use an affine transaction. Staging updates only the transaction, and
commit validates the staged checksum before replacing or appending one record;
abort leaves the document unchanged. The bounded document rejects invalid IDs,
field counts, unknown records, corrupt checksums, and capacity overflow.

`test/save_schema.elisa` covers commit, readback, migration, checksum
validation, and abort isolation. File-level journaling and crash recovery still
belong to the native persistence layer.
