# Compiler 8006b660 installation

Installed the clean main product using `bash ../Elisa-compiler/scripts/install_stage1.sh`.
Snapshot revision: `8006b66051ff68f8212bcbed052d066ab5c2bd8f`.
Product SHA256: `0a28b0f4ed4d022a49e956804822c20a38fabcdb0d95a1873a6721e491f4f359`.
Runtime SHA256: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
The installed product matches its provenance. The runtime is unchanged from the
previous snapshot. Compiler main subsequently advanced to documentation-only
`665f40d7`, correcting the guide's accumulator lint status to working.

Command:

```sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1 /opt/homebrew/bin/python3.14 scripts/run_tests.py "$HOME/.elisac/elisac-stage1" --no-cache -j4
```

Result: **215/215**, zero cached compiles, 65 seconds. This validates the runtime
test manifest, not physical audio or the complete shared/native gate.
Log: `build/validation/compiler-8006-runtime.log`; installation log:
`build/validation/compiler-8006-install.log`. The strict O2 pair built successfully as generation
`65e022cea38248fc9506db4979206eef`; build log:
`build/validation/proof-8006-build.log`. The uncached sweep remains **69/73**,
with the same ActionInput context/deadzone, audio animation events and sound event
assets failures (`build/validation/compiler-8006-proof-sweep.log`).
Fixed-array admission, scalar float ownership and IEEE literal forgery controls
all pass (`build/validation/compiler-8006-focused.log`). Full compatibility matrix
and shared/native qualification remain open.

## Shared check terminal result

Ran `scripts/check.elisascript` through the installed ElisaScript launcher with
`DEVELOPER_DIR=/Library/Developer/CommandLineTools`, `PYTHON_BIN=/opt/homebrew/bin/python3.14`,
`ELISA_COMPILER_BIN=$HOME/.elisac/elisac-stage1`, the paired engine prover,
`WICKED_ROOT=../WickedEngine` resolved absolutely, and
`ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`. Exit status **1** at the proof stage.

All preceding stages passed: 215 runtime tests (cached compiles), SDL3 platform,
three Godot probes, native unit suites, interactive validator, scene serialization,
skin limits/influences, locale/artifact checks, Metal render/motion, four native
navigation tests, and owner/privacy rejection fixtures. Locale errors in the log
are deliberate negative self-test inputs. Physical audio is unverified. The proof
stage has 69 cached passes and four freshly failed reports; validation.json is
not emitted because the overall gate failed. This is not full native-gate evidence.
Log: `build/validation/compiler-8006-shared-check.log`.

## Native gate prerequisites and sanitizer evidence

The canonical native-gate command fails at source checking with the installed
ElisaScript: run_process expects two arguments, while the gate uses a third
timeout argument. Building current ElisaScript `36a3374a` with compiler 8006
also fails (280 diagnostic log lines, including immutable assignments, cstr
return mismatches, invalidated argv views, and missing final else branches).
Log: `build/validation/elisascript-8006-build.log`. A compatible rebuilt launcher
is required; do not remove the explicit five/ten-minute deadlines.

An experimental wrapper reached the gate stages but was removed after confirming
the old launcher's default 120-second deadline would undercut those deadlines.
Its attempt is retained separately and does not qualify the canonical gate:
`build/validation/compiler-8006-native-gate-attempt.json` and
`compiler-8006-native-gate.log`. Headless status is 0: ASan/UBSan boundary and
navigation tests, audio stream/lifecycle ASan/UBSan and TSan, and asset-worker
TSan all pass. Source length and module hygiene pass. Dependency and native-build
statuses are 1: Wicked checkout `18066af2ac774249ee523be8eb494f04410c89a2` differs
from manifest `fd790f55b3237a9d266335ec742faeacc3cc9228`. Application is explicitly
skipped. Restore the pinned dependency in an isolated checkout with matching
archives, or qualify an intentional pin update before rerunning the gate.

## Pinned Wicked recovery and geometry gate repair

The dependency mismatch above was caused by selecting the unpinned sibling.
The existing clean `../elisa-boxing-wickedengine` is already at manifest revision
`fd790f55`; using its build directory passes the 13-library dependency checker.
No dependency pin update or new checkout is needed. With this root, the
`wicked_probe.elisascript build-gate` progresses to the geometry-loader tests
and exposes six failed exact inverse-bind comparisons out of 160 cases.
Log: `build/validation/compiler-8006-wicked-build-gate.log`.

The expectations retained raw authored matrices after `9f12acdb` introduced
bind-shape normalization. Updated ordinary/UV1 expectations use the existing
COOKED_INVERSE_BIND_MATRICES constant. Independent-root Z is 0.5, second-rig
ancestor Z is 0.25, and the 65-node rig Z is 0.75 + 61 * 0.001. Exact float32
comparison remains; malformed packages retain all rejection checks. Added
checker diagnostics identify the failed check and inverse-bind component.
`python3.14 scripts/test_geometry_subsets.py` now passes **160/160**, plus three
hash-verified LOD levels and seven malformed LOD controls. Logs:
`build/validation/geometry-subset-diagnosis.log` (old expectations fail),
`geometry-subset-bind-shape.log` (corrected expectations pass).
Full native build and application smokes still require rerunning; the launcher
timeout compatibility prerequisite remains open.
