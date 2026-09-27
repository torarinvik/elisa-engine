# Native-first gate validation

`scripts/native_gate.elisascript` composes the native checks without making
Godot a prerequisite. It accepts `quick`, `headless`, or `native`:

```text
elisascript scripts/native_gate.elisascript quick
elisascript scripts/native_gate.elisascript headless
elisascript scripts/native_gate.elisascript native
```

Every run clears `build/native-gate.json` before doing work and writes a fresh
schema-2 JSON record with dependency, source-length, module-hygiene, headless,
application, and Wicked stage states. A stage is explicitly `pass`, `fail`, or
`skip`, so a quick run cannot report an omitted native stage as green. The
record also includes the checkout revision, host platform, Python and selected
developer-directory provenance, timestamp, and `hardware_verification` (`verified`
only when both application and Wicked hardware stages pass). The shared
`build/validation.json` records compiler, prover, and ElisaScript hashes alongside
the engine source manifest and dependency pins. Quick mode is suitable for a clean checkout
policy check; headless adds the AddressSanitizer/UBSan boundary harness; native
adds the SDL3/Wicked graphics, package, navigation, coordinate, pacing, and
deterministic-frame gate.

The launcher resolves every repository check from its own source path, so it
works when invoked from outside the checkout. Native mode runs the Wicked build
gate first to validate the toolchain, then each of the 19 PhysicsRuntime and
gameplay application cases in its own child process. The matrix includes the
character-course state-transition, character-restart, and traversal smoke. It
then runs the render-scene, frame, rerun, and artifact verification stages.
Individual application children stay
within ElisaScript's default two-minute process deadline and report their own
exit status. Each case builds a temporary app harness, so the matrix takes several
minutes on a local GPU workstation. Each render-scene mode is also a separate
child. Captured output is relayed to the gate caller with the child exit status
preserved.

Run the shared source/proof check and native gate separately. For this layout,
the compiler, prover, and pinned Wicked checkout are sibling projects; the selected
Python must provide the standard-library features used by asset packaging. Native
build helpers default to the Wicked path named in `native/dependency-manifest.json`
and accept explicit roots when the checkout is elsewhere. A representative setup is:

```text
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
export PYTHON_BIN=/opt/homebrew/bin/python3.14
export ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh"
export ELISA_PROOF_BIN="../elisa-proof/build/elisa-proof"
export WICKED_ROOT="$PWD/../amazing-labyrinth-wickedengine"
export WICKED_BUILD="$WICKED_ROOT/build-elisa-sdl3"
elisascript scripts/check.elisascript
elisascript scripts/native_gate.elisascript native
```

The native command requires the configured Wicked build, SDL3 libraries, and a
graphics session. Godot compatibility runs as part of `scripts/check.elisascript`;
it is not a prerequisite for the native-only gate. On macOS 27, `DEVELOPER_DIR`
may need to select Command Line Tools when the Xcode-selected linker cannot read
the installed SDK stubs. Set `PYTHON_BIN` to Homebrew Python when the system
Python is an Xcode shim or lacks the packaging runtime features.

## Hosted CI evidence

`.github/workflows/check.yml` runs each hosted step through
`scripts/ci_stage.py REPORT_DIR JOB STAGE -- COMMAND`. The recorder streams
the command, keeps its output in `build/ci/logs/JOB-STAGE.log`, and appends
the stage's exit status to `build/ci/JOB.json`; a missing tool is recorded as
status 127 and fails the step. Every hosted report states
`evidence_class: hosted-portable` and `hardware_verification: unverified`.
The cook matrix uploads `hosted-portable-cook-<os>` and the macOS
library/sanitizer job uploads `hosted-headless-native`, both with `if: always()`
so failed runs keep their logs for 30 days. These artifacts never stand in for
the GPU workstation record above: SDL3/Metal rendering, application smokes, and
the pinned Elisa compiler, prover, and ElisaScript still run only through the
local native gate. `python3 scripts/test_ci_stage.py` covers pass/fail
recording, stage replacement on rerun, missing tools, and malformed names.

## Recorded toolchain snapshot (2026-09-27)

`scripts/check.elisascript` and `scripts/native_gate.elisascript native` both
passed on macOS 27.0/Apple M5 against one pinned Stage1 product (`ELISA_STAGE1_BIN`
pointing at a copied binary, sha256 `7ccb9831…9ce3`, so the concurrently edited
compiler checkout could not change it mid-run). `build/validation.json` records
that product hash plus the compiler entry script (`f242c033…`, Elisa-compiler
`b841e64`, dirty), prover (`0322f969…`, elisa-proof `f8d824f`, dirty) and
ElisaScript (`80fe9f28…`, elisa-script `37b37cc`, dirty). `build/native-gate.json`
recorded pass for dependency, source length, module hygiene, headless, application
(including `character-course-smoke`) and native stages, with
`hardware_verification=verified`. Because the sibling checkouts were dirty, the
hashes, not the commits, identify the tools; a fresh-checkout provisioning run
remains open.
