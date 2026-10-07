# Signed product growth — 2026-10-08

## Defect and implementation

The full matrix dependency-row probe could not verify `Retime::step`:
`amount * speed >= amount`, with amount bounded from 0 to 100 and speed from
1 to 100. Its finite enumeration exceeds the search budget.

Prover commit `0d3f1796` adds matching producer and kernel rules: a nonnegative
signed base multiplied by a same-width signed factor at least one cannot shrink
when the complete product stays in range. Both operand orders and comparison
orientations are supported; strict comparisons are not inferred. Primitive
witness and operator gates remain required.

Both rules consume only direct name/literal order bounds, including their
conjunctions. Arithmetic premises do not establish these bounds: accepting
`amount + 1 <= 101` as an upper bound would be unsound when the addition wraps.
The product still passes the existing signed-expression range audit. Missing
upper bounds, overflow, negative bases and zero factors remain refused.

## Acceptance

- `test_signed_product_growth.py`: nine cases pass both JSON routes with zero
  semantic errors, replay gaps and trusted assumptions: direct, commuted and
  reversed comparisons accepted; zero factor, negative base, i8 overflow,
  missing upper bound, overflowing-premise candidate and strict claim refused.
- Existing signed division boundary and scalar primitive witness controls pass.
- Uncached engine proof sweep: **73 total, zero cached, status 0**. Log:
  `build/validation/proof-product-growth-engine-sweep.log`.
- Source length and diff checks pass.
- Compiler remains immutable ddbc803d. Clean paired generation:
  `74543fa7f72c4fb9b4c194ed15d0c814`; build log:
  `build/validation/proof-product-growth-clean-build.log`. The repeated clean-pair
  sweep also passes all 73 reports; log:
  `build/validation/proof-product-growth-clean-engine-sweep.log`.

## Dependency-row and full qualification remain open

Once `Retime::step` verifies, its caller proceeds to check preconditions rather
than stopping at the unverified-callee refusal. The unchanged source now has
20 obligations, 18 certificates, 17 replayed and a gap at `range_step` line 40.
The product goal itself replays. Artifact:
`build/validation/proof-product-growth-dependency-row.json`.

The dependency-row regression is still failing. Next isolate the caller summary
replay for the bare same-module call with a repeated function name. Retain the
literal precondition repair and every invalid-call control. The full matrix,
shared and native gates remain unqualified.
