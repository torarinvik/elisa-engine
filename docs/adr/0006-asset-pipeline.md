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

## Persistent SQLite catalogue (2026-09-18)

The plan lists SQLite for persistent tooling and catalogue. `cook_assets.py`
now records each cooked asset in `build/catalogue.db` (a `assets` table keyed by
source path, carrying the content hash and normalized counts) using the Python
standard library, so the toolkit can look up a source by content hash without
re-parsing a package. The runtime does not read this database; the package
remains the runtime artifact. `record_validation.py` verifies the recorded row
agrees with the shared fixture's triangle count and records the database hash
under `asset_catalogue_database`, so the catalogue is gated rather than assumed.

## KTX container for cooked textures (2026-09-18)

The cooked textures traveled only as a project-local `base64` text package, so
no foreign host could open them without knowing that format. The cooker now
also writes KTX1 containers for the RGBA and BC1 payloads
(`maze_tile_tex.ktx`, `maze_tile_tex_bc1.ktx`), with the block payload keeping
its DXT1 format ID. The Godot host opens the container with
`Image.load_ktx_from_buffer` and sees a compressed image before it decompresses;
the native probe parses the container header and verifies the payload instead of
trusting the extension. Both probes are gated. KTX2 and Basis
supercompression/GPU transcode still need an encoder this environment does not
have, so the container is the implemented part and the transcode stays
deferred.

## Chunk streaming (2026-09-18)

`src/assets/streaming.elisa` divides a cell grid into fixed 2x2-cell chunks and
keeps a bounded resident set as the player moves: chunks whose cell rectangle
intersects the player's radius load, chunks outside unload, and the resident
count never exceeds the declared budget because the farthest desired chunks are
evicted first (counted as evictions). `examples/maze/game.elisa` drives it from
the real player position on start, restart, each move, and each hazard reset,
and `game_valid` requires the residency to be coherent. `test/maze_game.elisa`
pins the load on start, the unchanged budget after a move, and the coherent
restart.
