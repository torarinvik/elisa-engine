# Capability negotiation validation

The portable backend profile in `src/backend/capabilities.elisa` now has an
explicit `RequirementState` resolver. `negotiate_requirements` distinguishes a
ready profile, a profile that needs an allowed fallback, an unavailable
required feature, and an invalid profile or oversized requirement list.
Callers must provide a per-feature `BackendCapabilities` fallback map in
addition to allowing fallback globally. A missing service without a matching
fallback remains `Unavailable`; the helper count includes only concrete
fallbacks available for currently missing features. The resolver is bounded
by the existing eight-feature contract; another helper reports the first
missing feature.

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
stays `Unavailable` both when no fallback is declared and when fallback is
globally disallowed. It becomes `Fallback` only when the caller declares a
silent-audio handler; a nine-feature request remains `Invalid`. It also tests
each of the eight service fallback fields independently, decodes optional
native features and queried worker/viewport limits, and rejects a renderer
without viewports or optional features claimed without rendering.

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
available; Physics and Audio remain unavailable until public engine services
own those integrations. The lifecycle smoke checks those capabilities, queried
viewport and memory limits, and rejection after shutdown.

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
workers. The application smoke verified Physics and Audio stay unavailable
until Elisa-owned service adapters are integrated.
