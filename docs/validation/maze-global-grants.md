# Maze public API acceptance with default global grants — 2026-10-09

The existing Maze application now declares and scopes `Global.Read` and
`Global.Write` at the effectful world and native-runtime call boundaries. This
keeps the compiler's default grant checks enabled throughout the public-API
consumer build.

## Exact toolchain and outputs

- Compiler source revision: `b11e9121c64d94bbc8881db58ae850bf4933ceb9`.
- Stage1: `build/goal-compiler-latest-b11e/bin/elisac-stage1`, SHA256
  `5888942e08da166185c76a8e6a13d774c3aa57cf290f093f1c9ce03dd60a887e`.
- Runtime: `build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o`,
  SHA256 `013d317413defc5ffd2f79fb8dd791db6d6fd6a3217edc45fa62a81f4fc03df8`.
- Engine HEAD during the build: `d9ec7e7076ac24870a408df76e7898aa9c699531`,
  with tracked working-tree diff SHA256
  `7ab8c6e587126aea707479eaa04fa26c314f379f3ab330afebfce7115df0a286`.
- Native executable: `examples/maze/build/maze-native-smoke-global-grants-20261009`,
  SHA256 `a4fbfd1cd3bedcad5b231cd76cde769959db1a4af411de19c58d626811c366f2`.
- Cooked tile bundle SHA256:
  `d05d658c7db7f6b26b6e2067629c526b5eb75178e9f869da3f0d0838bc505212`.
- Cooked texture bundle SHA256:
  `47267ce17e5285df50036584d2abd9f5d7ef97b1840a52c8f89101704e236485`.

The executable provenance manifest is
`examples/maze/build/maze-native-smoke-global-grants-20261009.provenance.json`.
It binds the executable to the compiler/runtime pair, project manifest, native
archives, and the dirty engine source state above.

## Acceptance

The headless authored route passed start, win, lose, restart, pause, resume, and
exit using the pinned compiler/runtime pair:

```sh
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o" \
ELISA_MAZE_GAME_BINARY="$PWD/build/maze-game-b11e-global-rehome-20261009" \
elisascript scripts/maze_game.elisascript
```

The real native SDL3/Metal application built and its hidden scripted run passed:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o" \
/opt/homebrew/bin/python3 scripts/elisa_build_run.py run \
  --project examples/maze --main native_smoke_main.elisa \
  --output build/maze-native-smoke-global-grants-20261009
```

The packaged smoke passed all nine controls against that same executable and
the two bundles: staged execution with engine-checkout reads denied, a denial
control using the checkout's bundle, missing bundle, escaping symlink,
corrupted mesh section, corrupted texture section, missing texture bundle,
missing declared dependency, and successful execution after restoring the
bundle.

```sh
/opt/homebrew/bin/python3 scripts/packaged_maze_smoke.py \
  examples/maze/build/maze-native-smoke-global-grants-20261009 \
  examples/maze ../elisa-boxing-wickedengine/WickedEngine/shaders
```

The packaged-shader entrypoint was also updated with the same explicit grant
contract, built against the same Stage1/runtime, and passed the packaged Metal
shader smoke. Its executable is
`examples/maze/build/maze-packaged-smoke-global-grants-20261009`, SHA256
`b3c267e27afe38dafd0105def519c0f1f351bfe942ba1971c9cb477417b165d5`; its
provenance manifest records compiler/runtime hashes, build identity
`77cdf9b2c5af8df7`, engine commit `c94214ab` and the corresponding dirty source
diff. `scripts/packaged_shader_smoke.py` staged 392 Metal binaries and denied
reads of the Projects tree and writes to packaged shaders. Cold and warm startup
passed without shader compilation; the changed-package archive key, tampered
shader rejection and restored archive controls all passed. Measured cold
pipeline/first-frame times were 474/124 ms; warm times were 184/11 ms. Each run
rendered two frames.

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_COMPILER_BIN="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/bin/elisac-stage1" \
ELISA_RUNTIME_OBJ="$PWD/../Elisa-compiler/build/goal-compiler-latest-b11e/build/runtime/elisacore_runtime.o" \
/opt/homebrew/bin/python3 scripts/elisa_build_run.py build \
  --project examples/maze --main packaged_smoke_main.elisa \
  --output build/maze-packaged-smoke-global-grants-20261009

/opt/homebrew/bin/python3 scripts/packaged_shader_smoke.py \
  examples/maze/build/maze-packaged-smoke-global-grants-20261009 \
  examples/maze ../elisa-boxing-wickedengine/WickedEngine/shaders/metal
```

The first native build attempt used Command Line Tools Python 3.9 and stopped
at asset cooking because that interpreter has no zstd module. The accepted
rerun used Homebrew Python 3.14, which provides `compression.zstd`; both bundles
were cooked and the build and package acceptance then passed.

## Scope and remaining limits

This is consumer integration evidence for the explicit global-grant contract,
not a new pure policy, so it adds no engine proof file. Compiler grant-denial,
exact-grant, and `-permissive` bypass controls remain the semantic regression
guard. This smoke denies access to the engine checkout. It still uses the
external Wicked shader tree and linked Wicked/Homebrew libraries, so it is not
evidence for a self-contained, standalone release. The inferred-row
report-format issue and the independent prover, Studio, and Character Course
acceptance gates remain open; this Maze result does not close those items.
