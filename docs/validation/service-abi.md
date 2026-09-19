# Versioned service ABI validation

The embedding boundary now exposes a vendor-free `ElisaServiceV1` operation
table. It carries create, update, bounded query, and destroy entry points over
an opaque generation handle, an explicit caller/main-thread affinity, and an
allocator pair whose ownership stays with the service contract. The generated
Elisa archive exports the matching maze session functions; no Wicked or SDL
types cross the boundary.

Run from the engine root:

```sh
python3 scripts/embed_probe.py
```

The real host accepted the v1 descriptor and operation table, created a session,
updated it, queried the player position through a bounded output buffer, and
destroyed it. It rejected a version mismatch, truncated operation table,
missing allocator, undersized query buffer, stale handle, malformed span, and
truncated descriptor. Compile-time assertions cover handle size, buffer layout,
and allocator alignment.

The ABI remains scalar for this first service; future services may add typed
bounded payloads by extending the versioned table instead of exposing vendor
objects.
