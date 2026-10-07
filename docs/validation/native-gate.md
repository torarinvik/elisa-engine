# Native-first gate validation

`scripts/native_gate.elisascript` composes the native checks without making
Godot a prerequisite. It accepts `quick`, `headless`, `native`, or `smoke`
with a comma-separated application selection:

```text
elisascript scripts/native_gate.elisascript quick
elisascript scripts/native_gate.elisascript headless
elisascript scripts/native_gate.elisascript native
elisascript scripts/native_gate.elisascript smoke character-course-smoke,character-course-relaunch-smoke
```

Every mode clears `build/native-gate.json` before doing work and writes a fresh
schema-2 JSON record with dependency, source-length, module-hygiene, headless,
application, and Wicked stage states. Smoke mode records only the selected
application stage and marks the others skipped. A stage is explicitly `pass`, `fail`, or
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
gate first to validate the toolchain, then each of the 30 application regression
cases in its own child process. The matrix includes PhysicsRuntime, world/save,
render-resource and streaming, audio, input, and character-course checks. The
course state-transition and relaunch checks run in order through one runner
selection, with each check in its own child process; the relaunch check reads
the saved state from the first. The streamed-cell traversal check follows. The
gate then runs the render-scene, frame, rerun, and artifact verification stages.
Individual application children use a five-minute process deadline, except the
combined Character Course smoke and relaunch run, which has a ten-minute deadline
because it builds and runs both applications in sequence. Recent single-app builds
take about two minutes; the latest paired run took about seven. Each case builds a
temporary app harness, so the matrix takes several minutes on a local GPU
workstation. Each render-scene mode is also a separate child. Child output is
inherited by the gate caller, and the gate preserves each child's exit status.

Run the shared source/proof check and native gate separately. For this layout,
the compiler, prover, and pinned Wicked checkout are sibling projects; the selected
Python must provide the standard-library features used by asset packaging. Native
build helpers default to the Wicked path named in `native/dependency-manifest.json`
and accept explicit roots when the checkout is elsewhere. A representative setup is:

```text
export DEVELOPER_DIR=/Library/Developer/CommandLineTools
export PYTHON_BIN=/opt/homebrew/bin/python3.14
export ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh"
export ELISA_PROOF_BIN="../elisa-engine-proof/build/elisa-proof"
export WICKED_ROOT="$PWD/../elisa-boxing-wickedengine"
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

## Hosted steps added on 2026-09-30

The hosted cook job now runs `native_smoke_artifacts.py --self-test` on all three operating
systems. The hosted macOS native job runs `test_crash_report.py`. Both steps go through
`ci_stage.py`, and both passed locally through `ci_stage.py`. They have not yet run on a hosted
runner, because nothing has been pushed from this checkout.

## Recovery checks (2026-10-03; Q01 remains open)

The toolchain was rebuilt and pinned for this engine snapshot: Stage1 compiler
`98abcee` (`bin/elisac-stage1` SHA-256
`734fad7984b0c6de3b50e6d57585e3c8573f975c33e4560f647a1209f9ed828d`), proof
assistant `6d6b665`, and ElisaScript `6769aebb`. The fresh `build/validation.json`
records these identities against the engine source manifest with a clean tree.
`scripts/check.elisascript` passed: 210/210 Elisa tests, the SDL3 and Godot
probes, and 67/67 proofs.

Commit `1991f284` stages Wicked's executable-relative `libdxcompiler.dylib`
and optional `libmetalirconverter.dylib` beside development executables. This
matches Wicked's shader loader, which looks beside the running binary. The
build-run tests pass 28/28, and the focused `world-save-physics-smoke` passes
on SDL3/Metal; its artifact is `build/native-smoke/world-save-physics-smoke.json`.

The first expanded full-gate attempt initialized Wicked and passed the first
seven application cases, then stopped at `world-save-physics-smoke`: that
fixture explicitly includes runtime modules while the wrapper injected them a
second time. The harness now uses `--no-public-runtime` for this fixture, and
the focused rerun passes. The full 30-case gate has not been rerun after this
correction. The strict source-length check still reports
`native/render_scene_abi.h` at 604 lines; until that separate cleanup lands,
source policy fails and no `hardware_verification=verified` claim is made.

## macOS 27 rerun (2026-10-05; Q01 remains open)

The Stage1 snapshot launcher was repaired and installed from compiler commit
`7b27fa31`; its product SHA-256 is `4c265d5d…e080f05d`. A representative
`entity_id` compile and run passed. The shared gate reached all 211 test rows
but initially reported five compile failures. Those are fixed: the GLB
importer returns its linear document with an explicit move; viewport and
animation test arrays use caller-owned regions; animation package loading
writes into a caller-provided buffer; and the save-swap test names its error
variant using Stage1's directly resolved enum type. The five affected rows
passed uncached, and the full 211-row suite then passed with
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`. This was a test-suite run, not a full
`check.elisascript` run; proofs and later SDL3/Godot/native stages remain
unverified on this snapshot.

The native gate records dependency, source-length, module-hygiene, and
ASan/UBSan stages as passing on macOS 27.0.1 with SDL3 3.4.16 and Wicked
`fd790f55`. Its application stage stops at
`test/world_physics_pose_probe.elisa:67`: Stage1 rejects a captured non-static
reference passed to `pool_submit1`. The first three application smokes pass;
the remaining matrix, render captures, and hardware verification are
unverified. See `build/native-gate.json` and
`build/native-smoke/world-physics-pose-smoke.json` for this run's records.

Recheck on 2026-10-05 with fresh Stage1 provenance `bc8def2e` reproduces the
same `pool_submit1` diagnostic in the focused world-physics smoke. The
ElisaScript current-rebuilt product passes `--check` on the gate script and the
quick gate mode (dependency manifest, source length, module hygiene); the full
native gate remains pending that compiler fix.

## Retaining all test-runner failures (2026-10-05)

`scripts/run_tests.py` now writes every nonzero test row, its compile/run stage,
return code, stdout, stderr, flags, and arguments to the ignored generated file
`build/test-failures.json`. This keeps diagnostics for later rows that the
terminal summary previously reduced to test names. The report records the hash
of the selected compiler entry point, which may be a launcher script.

A focused rerun of the five compile-failing rows above produced all five full
diagnostics in that report, including the asset importer and viewport lifetime
errors. `python3 scripts/test_run_tests.py` passes its report-shape regression.
The report is local build output and is regenerated or removed by the next test
run; it is not checked in.

The asset-import diagnostic was an engine ownership omission: the linear
`GlbDocument` now returns as `return move doc` from
`src/assets/glb_document_import.elisa`. The other four failures were resolved
with caller-region output arrays in the viewport and animation tests, a
caller-provided output buffer for the animation package reader, and the direct
`SaveSwapError` name in the top-level save-swap test. All five affected rows
passed uncached; the full 211-row suite then passed. Runtime rows used
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`.

## Silent application validation (2026-10-05)

Native app smokes, packaged-maze and shader runs, interactive package startup,
standalone relocation checks, and induced crash-report launches force
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1` in the child process. The paired
Character Course relaunch smoke clears that flag only after the sound-producing
self-test, because it verifies reopening the saved output device. Its relaunch
mode does not start `CourseSounds`, so it creates no course clips, music, voices,
or streams; Wicked FAudio remains on SDL's dummy driver. See
[`character-course-live-input.md`](character-course-live-input.md#saved-output-relaunch-2026-10-05).
The standalone launch-environment regression and the complete native unit suite
pass; the packaged standalone validator also completed one relocated launch
with source and Homebrew denied.

The direct Character Course self-test and relaunch pair passed on 2026-10-05
(statuses 0; recorded durations 188.233s and 243.787s). Audio was forced
unavailable for the self-test; the relaunch opened the saved device but did not
start course audio. `scripts/native_gate.elisascript` selects a 600-second
deadline for this pair. The 2026-09-23 ElisaScript product still applies its
120-second default because it does not honor that requested deadline. ElisaScript
commit `6769aebb` contains the timeout fix, but its product rebuild was blocked
by Stage1 rejecting nested machine-arm branches in
`../elisa-script/src/runtime/output_transport_posix.elisa`. Q01 remains open
until the focused wrapper passes on a product that honors the selected deadline.

The 2026-10-05 full native-gate rerun on compiler Stage1
`541788548651d43dd466d0b5210955eb966eb18e` passed the first 19 application
smokes, then stopped at `character-course-smoke-and-relaunch`. The selected
ElisaScript executable is dated 2026-09-23, before the Oct. 3 custom-timeout
fix. The gate source already requests 600 seconds for the pair; the old product
uses its 120-second default instead. A direct Python rerun of the pair passed
(223.255s and 127.806s) with the self-test audio output unavailable; the
relaunch path reopened the saved output without playing course sounds.

The native-gate report now records the configured ElisaScript command and the
resolved executable path and SHA-256, so a rerun identifies whether it used the
stale product or a rebuilt one. Regression coverage checks both a resolved
product and a missing configured executable. Compiler commit `6b475d89` fixes
the parser errors in `output_transport_posix.elisa`, and its source parser
regression passes, but a rebuilt full ElisaScript product is not yet available.
The complete native gate and focused wrapper therefore remain unverified.

## Shared check and native wrapper status (2026-10-06; Q01 remains open)

`scripts/check.elisascript` ran with the installed Sep 17 ElisaScript product
(SHA-256 `80fe9f28cd0d36434e0bae68267b8652c833447c1b6bd01c0e4eaf7a8eca94bf`),
not a rebuild of the current source. It passed every stage before proofs:
212/212 Elisa tests, SDL3, headless Godot probes, native unit tests, scene and
artifact checks, viewport tests, navigation tests, and negative ownership
fixtures. The Stage1 revision reported by the gate was
`7b27fa312c5af923f044f6ee0e5e1de4f811f595`. The run set
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1` and `SDL_AUDIODRIVER=dummy`, so no
gameplay audio reached an output device. The six checks in
`scripts/test_application_native_smoke.py` also passed, including the forced
unavailable route. The proof failure stopped the command before it wrote
`build/validation.json`.

The separate Character Course smoke did not launch: the same installed
ElisaScript rejected the native-gate wrapper's third `run_process` timeout
argument at line 11. A bounded rebuild from ElisaScript commit `36a3374a` with
Stage1 revision `6b475d894331f0a81c3112167ef7fcf5c642a424` stopped at its
180-second watchdog on both attempts (peak RSS 741,808 KiB and 602,464
KiB). The retry was CPU-active; other Stage1 builds appeared during it. No
candidate executable was produced, and the installed product is unchanged.

The merged proof candidate at `f404eeda` (binary SHA-256
`0a774e27b0d67bb980c5b1e2e27248924b1dc1f290cf63d5a971da1f125985f0`)
fully replays 38/71 engine proofs; 31 have replay gaps and two are unsupported.
The previous local prover product (SHA-256
`eb0bbe0ef2d6d1960d1a6baca823484964dc0e5ba752160b507911ed61c4b509`)
fully replays 69/71; `action_input_context` and `action_input_deadzone` are
unsupported in both. The coverage drop needs triage to distinguish lost support
from newly exposed proof gaps. Keep Q01 open until the prover result and both
ActionInput invariants have accepted and rejected regressions.

## Typed signed return replay (2026-10-07; Q01 remains open)

In the `../elisa-engine-proof` worktree, commit `805b95a5` adds source-bound
replay for signed closed-integer return witnesses. The rule matches the exact
return span and expression, verifies a unique function declaration and its
signed width, checks the constant fits that width, and only accepts a closed
integer arithmetic value. `examples/typed_return_constant.elisa` proves 3/3
obligations and replays 3/3 certificates with the candidate built using pinned
Stage1 revision `23a0e16a854cddec3016746a0cb9480db0c4db22` (binary SHA-256
`2be331a9083a03728b8b9d4ea27548ee0dbfc90682a3c73503f7febb6d9bc665`). The
focused source-binding harness exercised the valid `-1` witness and rejected a
forged `7`; it then stopped at its package-replay stage because
`build/elisa-proof-replay` was absent from this single-product build. The
independent package check using the sibling replay product passed 2/2.

An uncached 71-input sweep fully replayed 62/71. The two ActionInput reports
remain unsupported. Four reports have replay gaps tied to local bindings or
helper summaries (`application_capture_timing`, `audio_music`, `audio_triggers`,
and `motion_overlay_policy`); `audio_virtual` and `sound_event_assets` still
have unproven obligations, and `glb_layout_index` has a source-obligation
inventory mismatch. The fixture's proof is complete, but those engine proofs
remain unproved, so the native shared check and Q01 are not green.
