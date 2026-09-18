# ADR 0006: Asset pipeline with stable IDs and offline cooking

**Status:** accepted (2026-09-18)

## Context

Serializing runtime pointers, backend handles, or compiler ordinals makes
every build and backend a distinct incompatible format. Parsing untrusted
bytes in the shipped runtime expands the attack surface.

## Decision

- Pipeline stages: source files plus import settings, validated
  normalized data, backend/platform cooking, versioned runtime package.
- Asset IDs are stable across world lifetimes and distinct from entity
  IDs and backend handles. Packages carry content IDs; the catalogue
  tracks cooked generations with bounded blobs (1 MiB cap).
- Same content re-registers idempotently; changed content must go
  through an explicit recook that bumps the generation. Stale
  (id, content, generation) triples never validate.
- Format support starts narrow (glTF meshes, glTF/PNG visuals); audio
  sources stay out of the visual cooker with explicit format errors.

## Evidence

`src/assets/descriptor.elisa`, `src/assets/package.elisa`,
`src/assets/database.elisa`, `examples/maze/bundle.elisa`
(package/content binding), `test/assets.elisa`, `test/package.elisa`,
`test/catalogue.elisa`, `test/maze_bundle.elisa`.

## Not covered

Byte-level importers, texture transcoding, audio streaming, chunked
scene streaming; catalogue persistence format (SQLite) not yet chosen.

## Bounded import and malformed-asset tests (2026-09-18)

`scripts/cook_assets.py` now bounds the untrusted import: document, buffer,
accessor, bufferView, and mesh counts have ceilings, the embedded buffer's
encoded length is checked before decoding, and every accessor's byte range must
lie inside the buffer instead of being silently sliced. `--self-test` crafts
seven malformed documents (wrong version, too many accessors, no embedded
buffer, out-of-range bufferView, accessor range beyond the buffer, negative
count, unsupported component type) and requires all to be rejected; it runs as
`asset_import_bounds` inside the validation gate. This keeps import parsing
bounded and gives the malformed-asset and oversized-count cases real tests
rather than a claim.
