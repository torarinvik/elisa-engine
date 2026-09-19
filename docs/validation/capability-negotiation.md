# Capability negotiation validation

The portable backend profile in `src/backend/capabilities.elisa` now has an
explicit `RequirementState` resolver. `negotiate_requirements` distinguishes a
ready profile, a profile that needs an allowed fallback, an unavailable
required feature, and an invalid profile or oversized requirement list. The
resolver is bounded by the existing eight-feature contract; helper queries
report the first missing feature and the number of fallback features.

`test/capabilities.elisa` compiles and runs these cases against a native
profile: rendering/physics/audio requirements fail with audio identified as
the first missing feature, the same request reports `Fallback` when fallback
is allowed, and a nine-feature request is `Invalid`. The test passed on
2026-09-19 with the stage-1 compiler.

The native `native/capability_probe.h` remains the source of truth for queried
graphics limits and optional ray-tracing/sparse-texture fallback. Its
`ELISA_FORCE_OPTIONAL_FALLBACK=1` gate proves that advertised device features
are not silently treated as required. A future versioned C ABI can populate an
Elisa `BackendProfile` directly; this slice keeps that transport boundary out
of the public vendor-free module.
