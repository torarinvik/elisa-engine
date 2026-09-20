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
record also includes the checkout revision, host platform, Python/SDK provenance,
timestamp, and `hardware_verification` (`verified` only when both application
and Wicked hardware stages pass). Quick mode is suitable for a clean checkout
policy check; headless adds the AddressSanitizer/UBSan boundary harness; native
adds the SDL3/Wicked graphics, package, navigation, coordinate, pacing, and
deterministic-frame gate.

The launcher resolves every repository check from its own source path, so it
works when invoked from outside the checkout. Native mode runs the PhysicsRuntime
application smoke, then the Wicked build, frame, rerun, and artifact verification
stages as separate processes. Native stage output streams directly to the gate
caller, preserving diagnostics and each child exit status.

On 2026-09-20 the complete native command passed on macOS 27.0/Apple M5:

```text
DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/native_gate.elisascript native
```

The report recorded pass for dependency, source length, module hygiene,
AddressSanitizer/UBSan, application lifecycle and failure cleanup, and Wicked
rendering. `build/native-gate.json` recorded `hardware_verification=verified`.
The sanitizer harness now links `native/miniaudio_implementation.cpp`, fixing
undefined miniaudio symbols on the updated macOS toolchain. The native command
requires the configured Wicked build, SDL3 libraries, and a graphics session.
Godot compatibility stays in `scripts/check.elisascript` and is reported
separately.
