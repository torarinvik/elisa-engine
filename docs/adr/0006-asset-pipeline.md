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
