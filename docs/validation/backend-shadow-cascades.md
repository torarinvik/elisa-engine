
## Hardening (2026-09-29)

`texel_um` and `blend_permille` now reject radii, distances and bands above
10^12 mm before multiplying, so the i64 arithmetic cannot overflow. A harness
was tried: `texel_um`'s `result >= 0` proves, but the blend bound needs a
signed quotient rule the prover lacks, and the `ElisaMath` float helpers it
includes are unsupported. A signed rule for `elisa-engine-proof` is drafted
but could not be built while the sibling compiler rejects the prover's
sources, so no harness is committed. `test/backend_shadow_cascades.elisa`
still exits 0.
