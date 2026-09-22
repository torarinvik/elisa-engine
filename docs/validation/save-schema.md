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
validation, and abort isolation. `scripts/save_journal.py` adds the file-level
transaction boundary: canonical bytes are fsynced before a journal record, the
replacement is atomic, and recovery verifies the journal hash before completing
an interrupted write. A corrupt or mismatched journal is discarded while the
last complete target remains readable. Directly truncated, invalid-UTF-8, or
non-object payloads raise `SaveError` with a byte or structural diagnostic
instead of leaking a decoder exception. Its self-test runs in the shared gate.
