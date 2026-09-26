# Packaged loading outside the checkout

A03 requires cooked content to load outside the source checkout.
`scripts/packaged_maze_smoke.py` checks this with the native maze. It stages
three files in a temporary directory outside the checkout:

- the built `maze-native-smoke` executable
- `assets/maze_tile.elpk`, the maze's cooked tile mesh
- `assets/maze_textures.elpk`, the wall texture, which the tile bundle declares
  as a dependency (`docs/validation/bundle-dependencies.md`)

It then runs the executable under `sandbox-exec` with this profile:

```scheme
(version 1)
(allow default)
(deny file-read* file-write* (subpath "<checkout>"))
```

The process can't read or write anything in the engine checkout. The
executable's working directory is a separate empty directory, and
`ELISA_PROJECT_ROOT` points at the staged directory. The script removes every
other `ELISA_*` variable, then sets the window title, size and hidden flag from
`examples/maze/elisa.project.json`.

`native_smoke_main.elisa` then runs the maze's finite self-test:

1. register both bundles
2. present the snapshot
3. run the scripted game with its live-instance checks
4. clear the scene and require zero live instances

Asset registration failure is exit 17.

## Cases

| Case | Expected exit |
| --- | --- |
| staged maze, checkout denied | 0 |
| control: `ELISA_PROJECT_ROOT` is the checkout's `examples/maze`, whose bundle the sandbox denies | 17 |
| the staged tile bundle is missing | 17 |
| the staged tile bundle is a symlink to an identical copy outside the project root | 17 |
| one byte in the middle of the tile bundle's `mesh` section is flipped | 17 |
| one byte in the middle of the texture bundle's `wallalbedo` section is flipped | 17 |
| the staged texture bundle is missing | 17 |
| the tile bundle is re-cooked to also depend on `maze_missing.elpk` | 17 |
| the original bundles are restored | 0 |

- The control case shows that the sandbox denies reads from the checkout. If
  the profile didn't deny the checkout, the control would exit 0.
- The symlink target has the same bytes as the restored bundle, which runs.
  So exit 17 means the escape was rejected, not that the data was bad. The
  rejection comes from path confinement in `native/cooked_geometry_package.h`:
  paths are canonicalised and must stay under the root.
- The script locates the `mesh` section from the ELPK version-1 index. That
  section is zstd-compressed. The flipped byte fails either zstd decoding or the
  CRC-32 check on the decoded bytes, and the section is rejected before upload.
- The `wallalbedo` section is the wall's brick image, stored uncompressed. The
  flipped byte fails its CRC-32 check when the texture registers
  (`docs/validation/bundle-textures.md`).
- The missing texture bundle is caught first by the tile bundle's dependency
  check, which logs `package dependency is missing` before the texture
  registers. In the last failing case the texture bundle is intact, so only
  that check can reject it.

`scripts/render_scene_native_smoke.py` runs these cases after the maze
application smoke, against the executable that smoke just built. The native
gate in `scripts/wicked_probe.elisascript` runs that smoke.

## Mutation checks

The mutation was applied in-process by replacing `sandbox_profile`, so the
script file was not edited.

| Mutation | Result |
| --- | --- |
| the sandbox profile keeps `(allow default)` and drops the deny rule | the control case exits 0 and is reported `FAILED`; the script exits 1 |

## Shader-root preflight

When `ELISA_ENGINE_SHADER_PATH` is set, the application validates it before
calling Wicked's initializer. The configured path must be a directory with a
platform shader directory (`metal` on macOS, `hlsl6` on Windows, or `spirv` on
other platforms) containing at least one compiled shader (`.cso` or `.spv`).
Missing, incomplete, overlong, or non-directory roots return
`Application::InitializeStatus.ShaderPathInvalid`; Wicked is not initialized
and the caller can report the configuration error cleanly. An unset or empty
variable keeps Wicked's default shader path behavior.

If `ELISA_ENGINE_SHADER_MANIFEST` is set, it must resolve to a regular
`elisa.shader-manifest.json`-format file directly inside that shader root. Before
Wicked initializes, the runtime checks the schema and canonical fingerprint,
requires a sorted, traversal-free inventory of compiled `.cso`/`.spv` files,
hashes every listed file, and rejects missing or unlisted binaries and paths that
resolve outside the shader root. The manifest is capped at 4 MiB, each shader at
1 GiB, and the total listed shader data at 4 GiB. Startup reports
`Application::InitializeStatus.ShaderManifestInvalid` for malformed, missing,
oversized, or content-mismatched manifests.

The native application smoke calls a test-only probe with a missing root and
requires this status. It also runs the normal startup and failure-cleanup
cases to prove that valid configured roots still initialize and shut down.

The macOS packager writes `shaders/elisa.shader-manifest.json` beside the
compiled shader tree. Schema 2 records the sorted Wicked backends represented by
the files plus sorted binary paths, byte sizes, and SHA-256 digests in a
deterministic `fingerprint`; the relocated launcher exports
`ELISA_ENGINE_SHADER_MANIFEST` along with the shader root. The runtime checks
that its active backend is declared and matches the actual files, then
recomputes those digests before loading the packaged shader tree. Edited,
missing, unlisted, or backend-mismatched binaries fail preflight. Schema 1
manifests remain accepted when their inventory contains the active backend.

## Limits

- **Shaders.** Shaders still come from the Wicked checkout through
  `ELISA_ENGINE_SHADER_PATH`. That path is outside the engine checkout, so the
  sandbox doesn't deny it. Packaged shaders are R13.
- **Shader packaging.** Preflight rejects missing or incomplete configured
  roots; schema 2 identifies bundled backend families and compiled binaries.
  `scripts/prepare_wicked_shaders.py` can prepare Wicked's complete Metal
  permutation set for a project and write the content manifest consumed by the
  packager. Per-project permutation selection, pipeline-cache keys, and
  cold/warm hitch measurements remain R13 work.
- **Dynamic libraries.** The executable links SDL3, FreeType, HarfBuzz and zstd
  from absolute `/opt/homebrew/opt` paths. It also keeps an rpath into the
  Wicked checkout. Neither location is inside the engine checkout, so the
  sandbox allows both. A self-contained, relocatable release is Q02.
- **Asset root.** The application finds its assets through
  `ELISA_PROJECT_ROOT`, or the working directory when that variable is unset.
  There is no fallback to the executable's directory.
- **Related records.** Bundle-backed textures are covered in
  `docs/validation/bundle-textures.md`, and dependency declarations in
  `docs/validation/bundle-dependencies.md`.
- **Platform.** The check needs macOS `sandbox-exec`. On other platforms it
  exits 2.

## Validation on 2026-09-21

- Standalone `python3 scripts/packaged_maze_smoke.py build/maze-native-smoke
  examples/maze ../WickedEngine/WickedEngine/shaders` passed all six cases.
- `scripts/render_scene_native_smoke.py` passed on SDL3/Metal, including the
  packaged cases.
- The mutation above failed the control case as expected.
- The full `PYTHON_BIN=/opt/homebrew/bin/python3 elisascript
  scripts/check.elisascript` suite passed, including both Elisa Proof suites
  (17/17 and 6/6).
- `scripts/check_module_hygiene.py`, `scripts/check_source_length.py` and
  `git diff --check` passed.
- After the maze split into tile and texture bundles,
  `scripts/render_scene_native_smoke.py` passed all nine cases above
  (`docs/validation/bundle-dependencies.md`).

## Validation on 2026-09-22

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools
  ELISA_ALLOW_STALE_STAGE1=1
  ELISA_COMPILER_BIN=.../Elisa-compiler/scripts/elisac_stage1.sh
  /opt/homebrew/bin/python3 scripts/application_native_smoke.py` passed both
  native application cases. The test-only probe rejected a missing shader root
  before Wicked initialization, and valid startup plus failure cleanup still
  passed on SDL3/Metal.
- The same native smoke also rejected a missing shader manifest before
  initialization while accepting the valid configured shader root.
- `PYTHONPATH=scripts /opt/homebrew/bin/python3
  scripts/test_package_macos_app.py` passed all six packaging tests, including
  deterministic and content-sensitive shader manifest fingerprints and
  launcher export of the relocated manifest path.

## Validation on 2026-09-25

- `/opt/homebrew/bin/python3.14 scripts/test_package_macos_app.py` passed all
  eleven tests. The suite builds and runs
  `test/shader_manifest_validation_native.cpp`, covering schema 1 compatibility,
  schema 2 backend matching, tampered and unlisted files, and backend mismatch.
- `clang++ -std=c++17 -fsyntax-only -DELISA_APPLICATION_TEST_PROBE=1 -I native
  -I /opt/homebrew/include native/application_test_probe.cpp` passed.
- `python3 scripts/check_source_length.py` and `git diff --check` passed.
- The first `scripts/application_native_smoke.py` invocation built the Elisa archive and native
  sources, but the final link failed in the separately seeded Elisa compiler's
  runtime with undefined `arena_free` and `ctx_string_views_eq` symbols. The
  standalone native verifier test passed independently during that attempt.

## Offline Metal shader preparation

Run the preparation command before packaging when an Elisa project needs its
own compiled shader tree:

```sh
python3 scripts/prepare_wicked_shaders.py --project path/to/game \
  --wicked-root path/to/WickedEngine --wicked-build path/to/build-elisa-sdl3
```

The command invokes the matching Wicked `offlineshadercompiler` in a temporary
workspace, with Wicked's DXC and Metal IR converter libraries available beside
it. It publishes compiled `.cso` permutations to `game/shaders/metal` only when
compilation succeeds, then writes `elisa.shader-manifest.json`. The compiler
does not modify the Wicked checkout. A failed compile leaves an existing
project shader library and manifest intact. The macOS packager copies this
directory and writes the final manifest after staging.

Validation on 2026-09-25: `scripts/test_prepare_wicked_shaders.py` passed its
staging, failure-preservation, and symlink-rejection cases. The real Wicked
offline compiler prepared 398 Metal permutations in a temporary project and
produced a schema 2 manifest listing all 398 binaries.

After the Elisa compiler runtime archive was rebuilt, the application native
smoke passed all 12 SDL3/Metal scenarios, including the two application
lifecycle probes and pixel-identical midpoint and final physics captures. The
earlier linker failure above records the state before that rebuild.

## Packaged notice files

A project's `elisa.project.json` can declare `package.notices` as a list of
project-relative notice files. For example:

```json
{"package": {"notices": ["third_party/SDL/LICENSE.txt", "third_party/Jolt/LICENSE.txt"]}}
```

The macOS packager preserves those files byte-for-byte beneath
`Contents/Resources/Notices`, retaining their relative directories. It rejects
missing, empty, duplicate, escaping, or symlinked notice files and output paths
that would erase a required input. Notice staging works independently of the
runtime resource allowlist. Sixteen packaging tests pass, including identical
license basenames in different dependency directories and invalid notice lists.
This provides distribution plumbing; the complete notice inventory for the
linked dependency closure is still outstanding in Q02.

## Preserving an existing app during rebuild

The macOS packager now assembles and signs a replacement in a sibling temporary
directory before publishing it. Failed assembly leaves the previous bundle
intact. Publication renames the previous bundle to a backup and restores it if
the replacement rename fails; if restoration also fails, the error names the
retained backup. Output paths overlapping packaging inputs are rejected,
including outputs inside an input directory that does not yet exist.

Twenty packaging tests pass. A real optimized maze app rebuilt through this
path passed two relocated launches with source, Homebrew, and outbound network
access denied. Publication uses two renames, so concurrent launches during
the replacement window and crash-atomic replacement are not guaranteed.
