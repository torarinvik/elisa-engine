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

`BackendProfile` carries typed RGBA8/BC1/R16F support and memory budget/usage.
`negotiate_limit` applies the same state machine to entity counts, texture
dimensions, upload bytes, worker budgets, and remaining memory. A request that
fits is `Ready`; an oversized positive request is either `Fallback` or
`Unavailable` according to policy; zero requests and invalid profiles are
`Invalid`. Texture-format negotiation mirrors the native policy: BC1 is
rejected for authored alpha and normal maps, RGBA8 is the safe fallback, and
missing formats stay unavailable rather than being advertised optimistically.

`test/capabilities.elisa` tests a native profile where audio is missing. It
stays `Unavailable` both when no fallback is declared and when fallback is
globally disallowed. It becomes `Fallback` only when the caller declares a
silent-audio handler; a nine-feature request remains `Invalid`.

The native `native/capability_probe.h` fills the vendor-free
`ElisaBackendProfile` from queried Wicked device limits. ABI version 2 carries
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
rejection, and exercising a synthetic texture-format fallback matrix. The
caller-declared service fallback cases also pass. Feeding a live host profile
into each runtime service and expanding the service-specific fallback matrix
remain open.
