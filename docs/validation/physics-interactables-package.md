# Physics Interactables package rehearsal — 2026-10-09

The existing physics-interactables native self-test was packaged as a relocated
macOS app using its saved executable, the project manifest's explicit empty
resource allowlist, and the prepared WickedEngine Metal shader tree. The package
was assembled without rebuilding the Elisa executable.

The project manifest now declares `package.resources: []`, matching this
procedural self-test's runtime needs. The bundle contains 9 dynamic libraries and
392 compiled Metal shader binaries plus the generated shader manifest. Its new
`Contents/Resources/package-provenance.json` records the exact package-manifest
SHA256, resource and notice policy, shader mode and manifest SHA256, bundle
identity, and hashes/sizes for all other files under `Contents`. The inventory
excludes the provenance file itself to avoid a self-referential digest. It stores
paths relative to `Contents` and contains no machine-specific source paths.

The packaged binary's build provenance retains the manifest identity from when
the executable was built (`2370c117…`). Package provenance separately records
the current packaging manifest (`63bed067…`), so source-build identity and
assembly settings remain independently auditable. The original executable hash
is `9b9bdb6a…`; the packaged, linked and signed binary hash is `e7b6f7ea…`.
Compiler Stage1 is `5888942e…` and the runtime object is `013d3174…`.

Packaging command:

```sh
python3 scripts/package_macos_app.py \
  --project examples/physics_interactables \
  --executable examples/physics_interactables/build/physics-interactables-global-grants-20261009 \
  --output examples/physics_interactables/build/validation/ElisaPhysicsInteractables.app \
  --name "Elisa Physics Interactables" \
  --bundle-id org.elisa.physics-interactables.validation \
  --shader-root ../elisa-boxing-wickedengine/WickedEngine/shaders \
  --compiled-shaders-only
```

The relocated-app validator passed two launches, each with source and Homebrew
access denied:

```sh
python3 scripts/validate_standalone_macos_app.py \
  --app examples/physics_interactables/build/validation/ElisaPhysicsInteractables.app \
  --runs 2 --timeout 120
```

The 35 focused macOS packaging tests pass, as does
`python3 scripts/check_source_length.py` with the 600-line maximum. The actual
bundle's manifest identity matches its project file; its package record covers
406 payload files and its shader-manifest digest matches the staged file.

This is package-layout and relocation evidence for an existing native
self-test executable. It does not qualify a fresh author/cook/build workflow,
interactive gameplay, distribution signing, legal notice completeness, a clean
machine, or the current compiler/proof tuple. Those Q02/Q07 and release gates
remain open.
