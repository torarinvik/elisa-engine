# Packaged loading outside the checkout

A03 requires cooked content to load outside the source checkout.
`scripts/packaged_maze_smoke.py` checks this with the native maze. It stages
two files in a temporary directory outside the checkout:

- the built `maze-native-smoke` executable
- `assets/maze_tile.elpk`, the maze's cooked ELPK bundle

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

1. register the bundle
2. present the snapshot
3. run the scripted game with its live-instance checks
4. clear the scene and require zero live instances

Asset registration failure is exit 17.

## Cases

| Case | Expected exit |
| --- | --- |
| staged maze, checkout denied | 0 |
| control: `ELISA_PROJECT_ROOT` is the checkout's `examples/maze`, whose bundle the sandbox denies | 17 |
| the staged bundle is missing | 17 |
| the staged bundle is a symlink to an identical copy outside the project root | 17 |
| one byte in the middle of the bundle's `mesh` section is flipped | 17 |
| the original bundle is restored | 0 |

- The control case shows that the sandbox denies reads from the checkout. If
  the profile didn't deny the checkout, the control would exit 0.
- The symlink target has the same bytes as the restored bundle, which runs.
  So exit 17 means the escape was rejected, not that the data was bad. The
  rejection comes from path confinement in `native/cooked_geometry_package.h`:
  paths are canonicalised and must stay under the root.
- The script locates the `mesh` section from the ELPK version-1 index. That
  section is zstd-compressed. The flipped byte fails either zstd decoding or the
  CRC-32 check on the decoded bytes, and the section is rejected before upload.

`scripts/render_scene_native_smoke.py` runs these cases after the maze
application smoke, against the executable that smoke just built. The native
gate in `scripts/wicked_probe.elisascript` runs that smoke.

## Mutation checks

The mutation was applied in-process by replacing `sandbox_profile`, so the
script file was not edited.

| Mutation | Result |
| --- | --- |
| the sandbox profile keeps `(allow default)` and drops the deny rule | the control case exits 0 and is reported `FAILED`; the script exits 1 |

## Limits

- **Shaders.** Shaders still come from the Wicked checkout through
  `ELISA_ENGINE_SHADER_PATH`. That path is outside the engine checkout, so the
  sandbox doesn't deny it. Packaged shaders are R13.
- **Missing shader root crashes.** A shader root with no shaders crashes the
  process with a segfault (exit 139). It doesn't fail with an error. Wicked's
  background initializer passes a missing shader to the Metal backend's
  `CreatePipelineState` (`wiTrailRenderer.cpp` `LoadShaders`). The crash
  happens inside `wi::Application::Initialize`, so a check after startup can't
  catch it. R13 must reject a missing or incomplete shader root before Wicked
  initializes.
- **Dynamic libraries.** The executable links SDL3, FreeType, HarfBuzz and zstd
  from absolute `/opt/homebrew/opt` paths. It also keeps an rpath into the
  Wicked checkout. Neither location is inside the engine checkout, so the
  sandbox allows both. A self-contained, relocatable release is Q02.
- **Asset root.** The application finds its assets through
  `ELISA_PROJECT_ROOT`, or the working directory when that variable is unset.
  There is no fallback to the executable's directory.
- **Remaining A03 work.** Custom dependency declarations and bundle-backed
  texture loading are still open.
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
