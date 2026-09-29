# Texture residency policy

Validated on 2026-09-29 on macOS 27.0 / Apple M5 with the pinned Stage1
compiler (`ELISA_ALLOW_STALE_STAGE1=1`).

This is R19 progress. It is the budget and eviction policy only; no cooked mip
chain, format-support query or Wicked upload is connected.

## Design

`src/backend/residency.elisa` (`BackendResidency`) tracks up to 8 textures of
up to 6 mips, each mip a quarter of the previous one's bytes.

- A texture is registered with only its coarsest mip resident, and that mip is
  never evicted, so a material always has a texture. Registration is refused
  for bad sizes, a full pool, or when the coarsest mip would exceed the budget.
- `want` records camera demand (clamped to the chain).
- `step(upload_budget)` first evicts one mip at a time (most surplus detail
  first) while the total is over the memory budget, then upgrades textures one
  mip per frame in index order, only while both the memory budget and the
  per-frame upload budget hold. It returns the bytes uploaded.

## Checks

- `test/backend_residency.elisa` exits 0 (codes 1–14): mip sizes, coarsest-only
  start, one-mip-per-frame streaming, upload cap, exact memory totals, budget
  never exceeded with two hungry textures, tight budgets, demand clamping.
- Negative control: removing the memory-budget check from upgrades makes it
  exit 10.
- Source-length check passes.

## Gaps

- No cooked mip chains, format fallbacks, asynchronous Wicked uploads or
  measurement of render-thread stalls; the R19 done condition is not met.
- Full gate still blocked at `world-test`.
