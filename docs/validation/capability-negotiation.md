# Capability negotiation validation

The portable backend profile in `src/backend/capabilities.elisa` now has an
explicit `RequirementState` resolver. `negotiate_requirements` distinguishes a
ready profile, a profile that needs an allowed fallback, an unavailable
required feature, and an invalid profile or oversized requirement list.
Callers provide a typed `BackendFallbackProviders` map in addition to allowing
fallback globally. Each map entry names the intended handler, such as
`JoltPhysics`, `MiniaudioSilent`, or `SynchronousUpload`; incompatible service
and provider pairs make the report `Invalid`. A missing service with no named
handler is `Unavailable`. A named handler with global fallback disabled is
reported as `FallbackDisallowed`. The resolver is bounded by the existing
eight-feature contract. Its ordered `BackendFeatureDecision` entries identify
each request as `Native`, `DeclaredFallback`, `FallbackDisallowed`,
`ProviderUnavailable`, `Unavailable`, or `Invalid`. Native and missing-handler decisions carry
`BackendFallbackProvider.None`; a declared or disallowed route carries its
provider identity. Repeated requests remain visible; unused entries stay
`Invalid`.
The report also retains the first missing feature and aggregate counts.

`BackendProfile` carries typed RGBA8/BC1/R16F support, optional ray-tracing,
sparse-texture and mesh-shader support, queried viewport/worker limits, and
memory budget/usage. Native fields the host does not measure are recorded as
unknown zero limits; they cannot satisfy a resource-capacity requirement.
`negotiate_limit` applies the same state machine to entity counts, texture
dimensions, upload bytes, worker budgets, and remaining memory. A request that
fits is `Ready`; an oversized or unknown positive request is either `Fallback`
or `Unavailable` according to policy; zero requests and invalid profiles are
`Invalid`. Texture-format negotiation mirrors the native policy: BC1 is
rejected for authored alpha and normal maps, RGBA8 is the safe fallback, and
missing formats stay unavailable rather than being advertised optimistically.

`test/capabilities.elisa` tests a native profile where audio is missing. It
stays `Unavailable` when no fallback is declared, reports
`FallbackDisallowed` when a handler is declared but fallback is disabled, and
becomes `Fallback` with the handler identity when allowed. It checks all eight
service routes, mixed native/fallback/unavailable outcomes, rejects a Jolt
provider assigned to Audio, and confirms a nine-feature request is `Invalid`.
It also decodes optional native features and queried worker/viewport limits,
and rejects a renderer without viewports or optional features claimed without
rendering.

The native `native/capability_probe.h` fills the vendor-free
`ElisaBackendProfile` from queried Wicked device limits, then passes that report
through the generated `maze_backend_configure` C ABI into Elisa's
`BackendProfile`. The native game start checks the required Rendering and Input
services against this live profile. The gate removes Input and confirms startup
becomes unavailable, restores it, and confirms malformed profile input leaves
the previous valid profile active. The SDL3 host's bounded runtime-hook
fallbacks return the supplied fallback values; its profile leaves Callbacks
unavailable, and weak definitions allow a real host callback registry to
override them.

Ordinary Elisa applications can also read the same validated profile through
`Application::backend_profile()` after initialization. This startup-time
snapshot is taken from the running SDL3/Wicked host and is returned as an error
union when the application is stopped, called from the wrong thread, or the
native profile cannot be represented safely. It reports only engine-owned
services: Input, Rendering, NativeWindow, AsyncUpload, and AssetLoading are
available; Physics and Audio remain unavailable in the live profile until their
native integrations are advertised and queried. The lifecycle smoke checks
those capabilities, queried viewport and memory limits, and rejection after
shutdown.

`Application::initialize_with_requirements()` negotiates a bounded set of
required services immediately after host initialization and before the caller
enters gameplay. `Ready` means every requested service is native. If every
missing service has a caller-declared fallback and fallback use is allowed, it
returns `FallbackRequired` with the host still initialized; ordered route
decisions tell the caller exactly which requested services need a fallback.
The caller must initialize each selected fallback before gameplay. Invalid or
unavailable requirements close the host and return the first missing feature,
counts, and per-request decisions in the report. A failed rollback is surfaced
as `ShutdownFailed`. The native lifecycle smoke checks the ready path,
unsupported Physics rejection and rollback, declared Physics and Audio fallback
decisions, and invalid-count rejection and rollback.

Before accepting a declared engine-owned fallback route, application startup
probes the selected Jolt or miniaudio adapter with a temporary
initialize/shutdown cycle. If a probe fails, the decision becomes
`ProviderUnavailable`, ordered counts and the first missing feature are
recomputed, and the host is rolled back. This proves that the adapter can start
at negotiation time; the caller still initializes and owns the live fallback,
which may fail later if the device or runtime changes. Caller-defined routes
are not probed. Synchronous upload and asset-loading fallback routes remain
unavailable until their adapters are implemented.

`src/backend/requirements.elisa` adds a fixed-capacity aggregate startup
contract for required services, optional graphics features, device limits, and
texture encodings. Limit requests specify preferred and minimum acceptable
values; a supported value between them is reported as a fallback and returned
in the report. Texture requests return the selected format for every slot, so
the caller can apply an allowed RGBA8 downgrade. Service and optional-feature
fallbacks require declared fallback maps. The combined report gives Invalid
priority over Unavailable, then Fallback, then Ready, while retaining each
category's details and counts.

`Application::initialize_with_profile_requirements()` runs this combined
negotiation before gameplay. It returns `FallbackRequired` with the selected
limits and texture decisions for the caller to apply, or shuts the host down
after Invalid/Unavailable results. Its report starts Invalid, including when
host startup itself fails. `test/capabilities.elisa` covers mixed ready and
fallback requests, a declared optional-feature fallback, an unavailable
unknown capacity, invalid preferred/minimum ranges, and capacity overflow.
The ordinary SDL3/Metal application smoke checks a successful combined startup
with native services, a viewport minimum, and RGBA8.

ABI version 2 carries
RGBA8, BC1, and R16F resource-format support, memory budget/usage, and the
actual high-priority and streaming worker counts. Typed C queries expose
individual capability bits, supported formats, and viewport/worker/memory
limits; unknown bits, renderer-inconsistent optional formats, malformed
versions, and invalid limit queries are rejected. `ELISA_FORCE_OPTIONAL_FALLBACK=1`
proves optional device features can be disabled without failing the base
renderer contract.

Validation on 2026-09-20: `scripts/check.elisascript` passed, including
`test/capabilities.elisa` and `test/maze_bundle.elisa`.
The two-pass Wicked native gate also exited 0 after querying the Apple M5
profile (`formats=0x7`, `workers=9/1`), checking unknown-bit and typed-query
rejection, exercising a synthetic texture-format fallback matrix, and
negotiating the live profile through Elisa before gameplay starts.

The ordinary application path also passed
`DEVELOPER_DIR="$(xcode-select -p)" python3 scripts/application_native_smoke.py`.
The full post-change `DEVELOPER_DIR="$(xcode-select -p)" ELISA_ALLOW_STALE_STAGE1=1
elisascript scripts/wicked_probe.elisascript` run passed two rendered native
passes, the ordinary Elisa application profile query, and the existing native
capability-policy checks. On the Apple M5 it reported profile bits `0xb3`,
optional bits `0x7`, formats `0x7`, 16 viewports, and 9 graphics / 1 streaming
workers. The live profile still reports Physics and Audio unavailable because
it reflects initialized native host services; Elisa can explicitly apply the
Audio fallback described in [`audio-runtime.md`](audio-runtime.md).

The checked-startup additions also passed the native application smoke: the
required Input/Rendering/NativeWindow set returned `Ready`; a Physics
requirement returned `RequirementsUnavailable` and left no initialized host; a
declared Audio fallback returned `FallbackRequired`; and a count beyond the
public bounded capacity returned `InvalidRequirements` with the host shut
down. The full `DEVELOPER_DIR="$(xcode-select -p)" elisascript
scripts/check.elisascript` suite passed, including its proof obligations and
certificate replay.

The latest SDL3/Metal application smoke also covers a concrete texture
downgrade. It requests BC1 for a normal map with fallback enabled, receives
`FallbackRequired` with RGBA8 selected in the per-texture report, confirms the
native application is still initialized while the caller can apply the choice,
then shuts down cleanly. This verifies the report is actionable; texture
loading and assignment still belong to the caller.

Aggregate negotiation validation on 2026-09-20:

- `DEVELOPER_DIR="$(xcode-select -p)" ../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/capabilities-test test/capabilities.elisa && build/capabilities-test` passed.
- `DEVELOPER_DIR="$(xcode-select -p)" ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py` passed; the real SDL3/Metal application returned `Ready` with its combined startup requirements.
- `python3 scripts/check_source_length.py`, `python3 scripts/check_module_hygiene.py`, and `python3 scripts/check_dependency_manifest.py` passed.
- `DEVELOPER_DIR="$(xcode-select -p)" ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/check.elisascript` passed the complete portable suite, both Elisa Proof proofs, and 23 certificate replays.

The fallback report does not instantiate fallback services. Gameplay code still
has to apply each declared service fallback. The opt-in PhysicsRuntime and
AudioRuntime adapters provide concrete Physics and Audio fallback paths, while
the live native profile continues to report those services as unavailable.

Physics and Audio fallback validation on 2026-09-20:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py` passed on macOS 27.0 / Apple M5. The hidden SDL3/Metal application creates static, dynamic, and kinematic Jolt bodies, rejects a destroyed handle as `PhysicsError.InvalidHandle`, advances eight fixed ticks and observes falling motion, then verifies host shutdown invalidates the Physics world. It also runs the negotiated silent Audio fallback after repeated application startup/shutdown.
- `test/parity/nested_const_module_collision_smoke.sh` passed stage0 and stage1. It guards the compiler fix for short nested-module references resolving sibling modules with the same child name, which had mapped the Physics ABI's invalid-handle code to the wrong Elisa error.
- Physics remains an explicit adapter-owned scene and is not yet advanced by the gameplay `World` scheduler; the native host therefore does not advertise it as a core service.

Per-service route follow-up, 2026-09-20: each bounded service decision also
names a typed fallback provider. Wrong-service provider pairs are invalid, and
the report distinguishes a missing handler from a declared-but-disallowed
fallback. The portable matrix verifies all eight services, mixed outcomes,
provider identity, and mismatch rejection. The real SDL3/Metal smoke continues
to verify native Input routing, unavailable Physics rejection, and Physics and
Audio fallback routing before the caller's live adapter instances run. F08
remains partial: startup preflight now checks that the selected Physics or
Audio provider can initialize at that moment, but the caller still initializes,
owns, and shuts down the live fallback. Physics and Audio are the only current
service adapters.

Typed provider-map validation on 2026-09-20:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ../Elisa-compiler/scripts/elisac_stage1.sh -emit exe -o build/capabilities-test test/capabilities.elisa && build/capabilities-test` passed, covering provider identity, native routes, disallowed fallback state, and wrong-service rejection.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" python3 scripts/application_native_smoke.py` passed both the SDL3/Metal application smoke and startup-failure cleanup smoke.
- The application smoke now rejects a silent-audio initialization with a sample rate below the named minimum, shuts the host down after the fallback error, verifies the backend profile is unavailable, then negotiates again and successfully initializes silent audio.
- Source-length, module-hygiene, dependency-manifest, and `git diff --check` policies passed.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="../Elisa-compiler/scripts/elisac_stage1.sh" elisascript scripts/check.elisascript` passed on the merged tree. The portable suite, including Godot 4.7.2 compatibility, passed; Elisa Proof proved both files (17/17 and 6/6 obligations) and replayed all 23 certificates.

F08 remains partial. Typed routes prevent selecting a provider for the wrong
service, and startup probes the Jolt/miniaudio adapters before accepting those
routes. Callers still must initialize the selected adapter, handle later
failure, and verify its lifetime before entering gameplay. Physics and Audio
are the only current adapters.

Provider-availability follow-up, 2026-09-21: portable tests verify unavailable
providers become `ProviderUnavailable`, mixed reports recalculate ordered
counts, and available Jolt remains a declared fallback. SDL3/Metal smoke fault
injection verifies failed Jolt-scene and miniaudio-device preflights roll back
startup, then successfully renegotiates both routes. The full Elisa suite and
native application smoke passed on macOS 27.0 / Apple M5.

F08 acceptance validation, 2026-09-21:

- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" PYTHON_BIN=/opt/homebrew/bin/python3 elisascript scripts/check.elisascript` passed the portable suite, Godot 4.7.2 compatibility probes, both Elisa Proof files (17/17 and 6/6 obligations), and all 23 certificate replays.
- `DEVELOPER_DIR=/Library/Developer/CommandLineTools ELISA_COMPILER_BIN="/Users/torarinvikbjarko/Documents/Coding Projects/Elisa Projects/Elisa-compiler/scripts/elisac_stage1.sh" PYTHON_BIN=/opt/homebrew/bin/python3 /opt/homebrew/bin/python3 scripts/application_native_smoke.py` passed the native user-data test, SDL3/Metal application lifecycle, Jolt and miniaudio fallback startup, injected partial-initialization rollback, retry, and startup-failure cleanup smokes.
- `test/capabilities.elisa` compiled and ran; module hygiene (91 production modules), the 600-line source policy, the SDL3-only dependency manifest, and `git diff --check` passed.

F08 is complete for the supported adapter set. Live profiles advertise only
queried native services; Jolt and miniaudio are typed, preflighted fallbacks
owned by `RuntimeServices::Session`. Synchronous upload and synchronous asset
loading remain `ProviderUnavailable`, and Physics/Audio are not reported as
native core services. They must remain unavailable until real adapters exist.
