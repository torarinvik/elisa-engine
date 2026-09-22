# Native service fallback probes and rollback

Before accepting a declared service fallback, `Application` asks the selected
engine-owned adapter to perform a temporary initialize/shutdown cycle. Jolt is
checked by creating and destroying its scene; miniaudio is checked by opening
and closing the selected silent or default device. This preflight is separate
from the caller's live fallback instance. A failed check changes the ordered
decision to `ProviderUnavailable`, recalculates the report, and shuts the host
down when requirements cannot be met. Caller-defined providers remain the
caller's responsibility. Synchronous upload and asset-loading routes remain
unavailable until their adapters exist.

The native application smoke covers failures that occur after a fallback adapter
has acquired a real backend resource. Fault injection is compiled only when
`scripts/elisa_build_run.py` receives `--native-test-probes`; ordinary builds do
not export these test hooks.

For Physics, one smoke arms a one-shot failure after Wicked/Jolt has constructed
the temporary scene. Aggregate profile negotiation reports `ProviderUnavailable`
and closes the host; a subsequent negotiation succeeds. A second failure is
injected after the caller begins `PhysicsRuntime::World()`, which receives
`BackendFailure`; the cleanup probe confirms no scene or live world remains.
After shutdown and renegotiation, the caller successfully creates a world and
runs the existing body, fixed-step, and shutdown checks.

For Audio, the smoke injects a one-shot failure after miniaudio opens a temporary
null device. Negotiation reports `ProviderUnavailable` and rolls the host back;
retrying the same route succeeds. It then rejects an invalid sample rate and
injects another failure after miniaudio opens the caller's null device. The
adapter shuts the device down before returning `DeviceUnavailable`. A subsequent
`active_voice_count()` returns `ApplicationUnavailable`, confirming that the
adapter did not publish itself as initialized. The caller closes the host,
checks the closed profile, re-negotiates `MiniaudioSilent`, and successfully
initializes, decodes, plays, and stops audio on retry.

Validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh python3 scripts/application_native_smoke.py` passed both SDL3/Metal application smokes, including preflight adapter failures, actual adapter partial-init failures, host rollback, and retries.
- `python3 scripts/test_elisa_build_run.py` passed all 7 CLI/build-runner tests.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` passed the portable suite, Godot 4.7.2 probe, and both Elisa Proof suites (17/17 and 6/6 obligations proved and replayed).
- Source-length policy, module hygiene (86 production modules), native dependency manifest (11 libraries, SDL3-only), and `git diff --check` passed.

## Composite service-session ownership

`src/runtime/services.elisa` adds an affine `RuntimeServices::Session` for ordinary Elisa
applications. It initializes the host from the caller's bounded requirements, then activates
the declared Jolt and miniaudio fallback providers. If a later provider fails, it closes the
already-started providers and the host. Shutdown closes Audio before Physics and then the
Application; if provider cleanup fails while the host remains live, the session retains its
ownership flags so cleanup can be retried. Physics stepping and body operations are scoped
through the session and reject stopped or unavailable ownership.

`test/runtime_services_probe.elisa`, called by the native application smoke, verifies a
Physics+Audio fallback session, rollback when the Audio configuration is invalid after
Physics has started, successful retry, duplicate-open rejection without disturbing the live
session, a Physics body create/step/query, and ordered shutdown. `examples/maze/capi.elisa`
also now passes the typed `BackendFallbackProviders` returned by
`no_backend_fallback_providers()` into capability negotiation; the previous code passed a
different struct with the same-shaped Boolean fields, so the compiler emitted declarations
without the two function bodies and the native archive could not link.

When the requested provider is `MiniaudioDefault` and the default output device is
unavailable, the start degrades to the silent miniaudio device instead of rejecting the
requirements. The host's provider-availability pass probes the silent provider whenever
the default probe fails, and `backend_requirement_apply_provider_availability` rewrites
the audio decision to a `DeclaredFallback` on `MiniaudioSilent`, so `open` reports
`FallbackRequired` rather than `RequirementsUnavailable`. If the device probe passes but
the real open still fails, `open` activates the silent device in place and records
`MiniaudioSilent` as the session's audio provider instead of rolling the host back. Both
routes match the runtime device-loss recovery. It was added after the Amazing Labyrinth
bundle, launched with `ELISA_AUDIO_FORCE_DEVICE_UNAVAILABLE=1`, rolled its host back,
reinitialised SDL3/Wicked/Metal in the same process and then hung forever in the Metal
frame-fence wait on the first rendered frames. The restart probe now also exercises a
bounded full scene before every in-process teardown: 40 authored Wicked cubes, a camera,
and a light are rendered for two frames, GPU completion is awaited, and all scene
component counts must return to zero. On macOS 27.0, focused three-cycle and 16-cycle
runs passed with `rendered=1 components_cleared=1`; the 16-cycle run measured zero GPU
delta and a 4,272-byte process-heap delta across 12 measured cycles. This validates
full-scene ownership and cleanup, while the imported Amazing Labyrinth reproduction
remains open because it is the scene that previously hung. `test/runtime_services_audio_probe.elisa`, run by
the native application smoke, opens a session with the default provider while the
test-only failure flag makes the device open fail, and checks the session is live on the
silent route with no voices and shuts down cleanly.

The affine owner now carries `Runtime::StepClock` state. `advance_simulation()` returns the
number of due fixed ticks, the scheduled tick count, and the interpolation fraction for the
game's validated `Executor` plan; shutdown and failed-start rollback reset the clock. The
session also routes audio decode, play, stop, bus gain, and voice-count operations through the
provider it activated, preserving Audio errors while returning `AudioUnavailable` or
`SessionStopped` for invalid ownership. The probe accumulates a 10 ms frame and a 24 ms frame,
checks the resulting two fixed ticks and interpolation remainder, runs those ticks through
the session-owned Jolt adapter, and tests audio playback, invalid handles, no-Audio sessions,
and post-shutdown calls.

`src/runtime/world_physics.elisa` adds an affine `WorldPhysics::Bindings` owner that maps
checked World entity references to body handles held in a `PhysicsRuntime`-owned opaque
handle pool. `bind()` creates a Jolt box at the entity's current position. `advance_and_sync()`
consumes the session clock, steps Physics once for each due tick, and publishes body positions
back to World after staging every pose; it preserves the entity's existing rotation and scale.
`unbind()` destroys the Jolt body before removing the mapping, so failed destruction leaves
cleanup retryable. The native application probe verifies a dynamic body falls from its starting
position, its World transform is updated, entity scale survives, duplicate links are rejected,
and unbinding the first of two bodies compacts the mapping without disconnecting the remaining
body. The native probe also executes an automatic Physics -> WorldSync plan through the generic
executor for each due tick; hierarchy-aware synchronization stages solver poses and publishes
them parent-first. The affine-context compiler regression and application smoke are recorded below.

Additional validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/native_gate.elisascript native` passed all stages on macOS 27.0 / Apple M5, including SDL3/Metal startup, failure rollback, cooked assets, deterministic Wicked rendering, live input, and the new service-session probe. The report recorded `hardware_verification=verified`.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/check.elisascript` passed the full portable suite, Godot 4.7.2 probe, and both Elisa Proof suites (17/17 and 6/6 obligations proved and replayed).
- Source-length policy, module hygiene (87 production modules), dependency manifest (11 libraries, SDL3-only active target), and `git diff --check` passed.
- Engine implementation commit `6e49ed6` contains the source used for this validation; the report records base revision `e715309` because the gate ran just before that source commit was written. After adding the fixed-step owner and session-routed audio commands, `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/native_gate.elisascript native` passed on macOS 27.0 / Apple M5 with `hardware_verification=verified`. The gate covered sanitizer boundaries, the SDL3/Metal service-session and failure-cleanup smokes, native asset cooking, both Wicked render passes, deterministic image checks, live input, and frame-time limits. `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/check.elisascript` passed, including Godot 4.7.2 and both Elisa Proof suites (17/17 and 6/6 obligations). Source-length and module-hygiene checks passed (88 production modules). Report: [`build/native-gate.json`](../../build/native-gate.json).

The ordinary `examples/maze/native_client.elisa` now opens and shuts down through
`RuntimeServices::Session`, and routes its event pump, frame snapshot, and exit request
through that owner. The frame-info accessor returns a value so callers do not hold a mutable
borrow into the native host. The service probe verifies that pump, frame, exit, clock, and
audio calls return `SessionStopped` after shutdown.

Validation after the maze-client migration on 2026-09-21:

- Engine commit `bf592f8` passed `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py`; both SDL3/Metal app runs passed, including session-owned audio, no-Audio fallback rejection, and stopped-session checks.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/elisa_build_run.py run --project examples/maze --main native_smoke_main.elisa --output build/maze-session-smoke` passed the hidden SDL3/Metal maze client, exercising session-owned startup, frame access, event pumping, and ordered shutdown.
- Source-length policy, module hygiene (88 production modules), and `git diff --check` passed.

The SDL3/Wicked profile still reports Physics and Audio as unavailable native core
capabilities; the session may activate them only when the caller's requirements explicitly
declare the matching fallback. `WorldPhysics::advance_and_sync()` now connects due session
ticks to checked entity transforms, while automatic dispatch of a general user system plan and
hierarchy-aware synchronization remain incomplete. Spatial audio, device-loss recovery, and
non-Physics/Audio providers also remain incomplete.

Validation of the World-to-Physics bridge on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh python3 scripts/application_native_smoke.py` passed both SDL3/Metal applications. The session probe attached two dynamic Jolt bodies to checked World entities, verified gravity-driven pose sync and scale preservation, rejected a duplicate binding, removed the first binding, confirmed the second continued syncing after compaction, and cleaned up both bodies.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN=../Elisa-compiler/scripts/elisac_stage1.sh elisascript scripts/check.elisascript` passed the portable suite, Godot 4.7.2 probes, both Elisa Proof suites (17/17 and 6/6), runtime scheduler checks, source-length policy, and module-hygiene policy.

## Generic executor dispatch with affine runtime contexts

The native `WorldPhysics` probe now dispatches its Physics -> WorldSync plan through
`Executor::plan_execute_with_contexts` on every due fixed tick. This exposed two stage1
compiler gaps: function values could not resolve private functions in the current module,
and a module-qualified fallible generic could lose its owner during specialization when
its callback arguments included affine runtime contexts. Elisa-compiler now covers both
behaviors with `test/parity/generic_module_error_callback_smoke.sh`, which compares
stage0/stage1 compilation and runs the specialized executable.

Validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools scripts/elisac_stage1.sh --seed` passed in Elisa-compiler.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools test/parity/generic_module_error_callback_smoke.sh` passed; the specialized executable returned its expected status.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py` passed both SDL3/Metal application and failure-cleanup smokes with the generic schedule dispatch.
- Engine source-length policy, module hygiene (89 production modules), and `git diff --check` passed.

## Caller-owned fallback routes

`RuntimeServices::Session::open` now accepts a valid `CallerDefined` route from the
negotiated report without treating it as an engine-owned adapter. It returns
`FallbackRequired` with the provider identity intact; the application initializes and
uses that handler itself. The native service probe requests the unavailable callback
service through this route, confirms no Physics or Audio adapter was activated, and
shuts the host down cleanly.

Validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py` passed both SDL3/Metal application and failure-cleanup smokes.
- Source-length policy, module hygiene (89 production modules), and `git diff --check` passed.
