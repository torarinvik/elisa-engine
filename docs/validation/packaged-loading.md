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

The native application smoke calls a test-only probe with a missing root and
requires this status. It also runs the normal startup and failure-cleanup
cases to prove that valid configured roots still initialize and shut down.

The macOS packager writes `shaders/elisa.shader-manifest.json` beside the
compiled shader tree. Its schema version, sorted binary paths, byte sizes, and
SHA-256 digests produce a deterministic `fingerprint`; the relocated launcher
exports `ELISA_ENGINE_SHADER_MANIFEST` along with the shader root. A changed
compiled shader therefore produces a new package identity before any runtime
pipeline cache is reused.

## Limits

- **Shaders.** Shaders still come from the Wicked checkout through
  `ELISA_ENGINE_SHADER_PATH`. That path is outside the engine checkout, so the
  sandbox doesn't deny it. Packaged shaders are R13.
- **Shader packaging.** Preflight rejects missing or incomplete configured
  roots, but versioned shader bundles, permutation manifests, offline
  compilation, pipeline-cache keys, and cold/warm hitch measurements remain
  R13 work.
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
- `PYTHONPATH=scripts /opt/homebrew/bin/python3
  scripts/test_package_macos_app.py` passed all six packaging tests, including
  deterministic and content-sensitive shader manifest fingerprints and
  launcher export of the relocated manifest path.
