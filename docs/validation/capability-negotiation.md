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

The native `native/capability_probe.h` now fills the vendor-free
`ElisaBackendProfile` in `native/capability_abi.h` from the queried device
limits. Its `ELISA_FORCE_OPTIONAL_FALLBACK=1` gate proves that advertised device
features are not silently treated as required, and the probe rejects an ABI
version mismatch before handing the profile to policy. Resource-specific
feature population and platform-specific worker counts remain follow-up work.
