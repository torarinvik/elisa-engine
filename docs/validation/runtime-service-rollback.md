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

Additional validation on 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/native_gate.elisascript native` passed all stages on macOS 27.0 / Apple M5, including SDL3/Metal startup, failure rollback, cooked assets, deterministic Wicked rendering, live input, and the new service-session probe. The report recorded `hardware_verification=verified`.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/check.elisascript` passed the full portable suite, Godot 4.7.2 probe, and both Elisa Proof suites (17/17 and 6/6 obligations proved and replayed).
- Source-length policy, module hygiene (87 production modules), dependency manifest (11 libraries, SDL3-only active target), and `git diff --check` passed.

The SDL3/Wicked profile still reports Physics and Audio as unavailable native core
capabilities; the session may activate them only when the caller's requirements explicitly
declare the matching fallback. Spatial audio, device-loss recovery, gameplay-World scheduler
integration, Audio operations through the composite session, and non-Physics/Audio providers
remain incomplete.
