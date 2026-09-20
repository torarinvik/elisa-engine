# Native service partial-startup rollback

The native application smoke covers failures that occur after a fallback adapter
has acquired a real backend resource. Fault injection is compiled only when
`scripts/elisa_build_run.py` receives `--native-test-probes`; ordinary builds do
not export these test hooks.

For Physics, the smoke arms a one-shot failure after Wicked/Jolt has constructed
the scene. `PhysicsRuntime::World()` receives `BackendFailure`; a test hook then
confirms that no scene or live world remains. The native capability profile is
still honest, the caller closes the host, confirms that its profile is gone,
re-negotiates the `JoltPhysics` route, and successfully creates a world on retry.
The succeeding route also runs the existing body, fixed-step, and shutdown
checks.

For Audio, the smoke first rejects an invalid sample rate, then injects a
one-shot failure after miniaudio successfully opens the null device. The adapter
shuts the device down before returning `DeviceUnavailable`. A subsequent
`active_voice_count()` returns `ApplicationUnavailable`, confirming that the
adapter did not publish itself as initialized. The caller closes the host,
checks the closed profile, re-negotiates `MiniaudioSilent`, and successfully
initializes, decodes, plays, and stops audio on retry.

Validation on 2026-09-20:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh python3 scripts/application_native_smoke.py` passed both SDL3/Metal application smokes, including Physics and Audio fault injection and retries.
- `python3 scripts/test_elisa_build_run.py` passed all 7 CLI/build-runner tests.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` passed the portable suite, Godot 4.7.2 probe, and both Elisa Proof suites (17/17 and 6/6 obligations proved and replayed).
- Source-length policy, module hygiene (86 production modules), native dependency manifest (11 libraries, SDL3-only), and `git diff --check` passed.

This verifies rollback and retry for the two implemented service adapters. The
native profile still does not advertise Physics or Audio as native services;
callers own fallback initialization and shutdown, and other services do not yet
have adapters.
