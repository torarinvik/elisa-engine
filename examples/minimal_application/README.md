# Minimal Elisa application

This example is an ordinary Elisa `main()`. The engine runner adds the default
`Application`, `ActionInput`, `RenderScene`, and `Geometry` modules from
`src/runtime/public.elisa`, builds the Elisa entry as an archive, checks that
it defines no game-owned C exports, and links the engine services. The project
contains no C++ build code or engine-relative module includes.

From any directory, run:

```sh
python3 "/path/to/elisa-engine/scripts/elisa_build_run.py" run \
  --project "/path/to/elisa-engine/examples/minimal_application"
```

The sample manifest sets the entry point, output path and window options. The
app opens a window, runs one frame, and exits so it is safe to use as a first
build check. A project can import `ActionInput` for game actions and
`RenderScene` to create and transform primitive instances through Elisa.

The runner uses `WICKED_ROOT` (defaulting to a sibling `WickedEngine` checkout)
and `WICKED_BUILD` (defaulting to `build-elisa-sdl3` inside that checkout).
Set `SDL3_ROOT` or `WICKED_SDL3_ROOT` to an SDL3 prefix and
`HOMEBREW_PREFIX` or `WICKED_BREW_PREFIX` for freetype, harfbuzz, and zstd; if
unset, the runner asks `brew --prefix` and `brew --prefix sdl3`. Direct include
and library directory overrides are also available as `WICKED_SDL3_INCLUDE_DIR`,
`WICKED_SDL3_LIB_DIR`, `WICKED_BREW_INCLUDE_DIR`, and `WICKED_BREW_LIB_DIR`.
Use `ELISA_COMPILER_BIN` and `CXX` to select the Elisa compiler and native linker.
Equivalent per-invocation options are shown by `elisa_build_run.py --help`.
