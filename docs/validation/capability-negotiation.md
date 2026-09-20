# Capability negotiation validation

The portable backend profile in `src/backend/capabilities.elisa` now has an
explicit `RequirementState` resolver. `negotiate_requirements` distinguishes a
ready profile, a profile that needs an allowed fallback, an unavailable
required feature, and an invalid profile or oversized requirement list. The
resolver is bounded by the existing eight-feature contract; helper queries
report the first missing feature and the number of fallback features.

`BackendProfile` carries typed RGBA8/BC1/R16F support and memory budget/usage.
`negotiate_limit` applies the same state machine to entity counts, texture
dimensions, upload bytes, worker budgets, and remaining memory. A request that
fits is `Ready`; an oversized positive request is either `Fallback` or
`Unavailable` according to policy; zero requests and invalid profiles are
`Invalid`. Texture-format negotiation mirrors the native policy: BC1 is
rejected for authored alpha and normal maps, RGBA8 is the safe fallback, and
missing formats stay unavailable rather than being advertised optimistically.

`test/capabilities.elisa` compiles and runs these cases against a native
profile: rendering/physics/audio requirements fail with audio identified as
the first missing feature, the same request reports `Fallback` when fallback
is allowed, and a nine-feature request is `Invalid`. The test passed on
2026-09-19 with the stage-1 compiler.

The native `native/capability_probe.h` fills the vendor-free
`ElisaBackendProfile` from queried Wicked device limits. ABI version 2 carries
RGBA8, BC1, and R16F resource-format support, memory budget/usage, and the
actual high-priority and streaming worker counts. Typed C queries expose
individual capability bits, supported formats, and viewport/worker/memory
limits; unknown bits, renderer-inconsistent optional formats, malformed
versions, and invalid limit queries are rejected. `ELISA_FORCE_OPTIONAL_FALLBACK=1`
proves optional device features can be disabled without failing the base
renderer contract.

Validation on 2026-09-20: `test/capabilities.elisa` and
`test/maze_bundle.elisa` both compiled and exited 0 with the stage-1 compiler.
The two-pass Wicked native gate also exited 0 after querying the Apple M5
profile (`formats=0x7`, `workers=9/1`), checking unknown-bit and typed-query
rejection, and exercising a synthetic texture-format fallback matrix. Broader
service-specific fallback matrices and feeding a live host profile into each
runtime service remain open.
