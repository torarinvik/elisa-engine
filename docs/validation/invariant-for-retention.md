# Untouched guards after invariant-bearing for loops

On 2026-10-07, prover `fc7ebaac` replaces blanket outer-state forgetting at
successful invariant-bearing `for` exit with the existing loop-entry mutation
and alias analysis. Written/reachable roots still become opaque; untouched
roots retain their entry values and facts. Checked invariant facts are then
reintroduced. Failed invariant analysis keeps its conservative exit treatment.

This repairs a demonstrated missing policy proof rather than changing engine
runtime behavior. The existing arbitrary-iteration analysis supplies the same
write, escaping-reference and unbounded-alias handling used at loop entry.

## Verification

- `python3.14 ../elisa-engine-proof/scripts/test_invariant_for_retention.py`:
  four fixtures through both JSON routes; read-only and zero-iteration loops
  accepted, changed field and insufficient entry guard refused. Zero semantic
  errors, replay gaps or trusted assumptions in every case.
- `python3.14 ../elisa-engine-proof/scripts/test_loop_state_joins.py`: passes
  positive rebind/arm/aggregate cases and existing false controls.
- The uncaptured value-search reproducer now passes 8/8, zero gaps:
  `build/validation/read-search-no-capture.json`.
- The implementation-linked ActionInput context report remains 232/264, with
  32 findings and zero gaps: `build/validation/read-search-action-input.json`.
- The uncached engine sweep remains 71/73; only ActionInput context/deadzone
  fail: `build/validation/proof-read-search-engine-sweep.log`.

The new suite is wired into the full prover matrix. Full matrix, shared and
native qualification remain open. Captured record read/write classification,
captured-result replay and copy-time snapshot authentication remain required;
this change does not establish the captured ActionInput search's final bounds.

## Untouched reference captures

Prover `aaabd616` exempts an untouched mutable-reference parameter capture from
write-back when the body has no calls/transfers/overloaded operators and every
recorded write is to a witnessed scalar. It copies the reference without moving
or changing the referent. Other captures and body effects remain conservative.

The focused suite now has six cases through both JSON routes: a captured
read-only loop with `invariant true` passes 8/8; a captured field mutation is
refused. Captured-loop entry controls and loop-state controls pass. The original
search reproducer improves to 7/8, zero findings/errors, one remaining replay
gap on the scalar invariant-entry certificate (`read-capture-repaired.json`).
Its post-search copy/indexed write now proves. This isolates source replay from
guard retention; it does not count as full acceptance of that reproducer.

ActionInput remains 232/264, zero gaps (`read-capture-action-input.json`); the
full uncached engine sweep remains 71/73 (`proof-read-capture-engine-sweep.log`).
Captured scalar invariant-entry replay and ActionInput bounds remain open.

## Entry replay with an unrelated capture

Prover `4eeb72e8` allows unrelated captures on the exact two-statement loop
initializer recognized by source replay. The accumulator's own capture is
refused; empty wrappers remain capture-free. Literal initializer, binding
positions, exact original invariant and absence of iteration facts remain
required. Initial equality is available only at the original invariant entry.

`test_captured_search_entry.py` passes six cases through both JSON routes,
with zero gaps/errors/trusted assumptions: original read-only search and zero
iterations accepted; wrong initializer, wrong preservation, post-loop record
mutation and insufficient entry bound refused. The original reproducer is now
8/8. Existing initializer and captured-loop entry suites pass, including stale
exit claims. The new suite is wired into the full matrix.

An additional in-loop record mutation is refused but has a separate scalar
preservation replay gap (`captured-entry-changed.json`: 6/8, one finding, one
gap). That diagnostic is not counted as a passing regression. ActionInput
remains 232/264 with zero gaps (`captured-entry-action-input.json`), and the
uncached engine sweep remains 71/73 (`proof-captured-entry-engine-sweep.log`).
Copied snapshot bounds, remaining ActionInput obligations and full qualification
remain open.

## Conditional assignment boundary

Prover `384fd8a1` retains the minimized
`test/repro/conditional_search_field_bound.elisa`: a conditional assignment of
a captured search result to an outer scalar, followed by the guarded field copy.
`conditional-search-baseline.json` has 8 obligations, 6 replayed, one finding
and one invariant-entry replay gap, with zero semantic errors. A temporary
scalar-only escaping-reference exemption left ActionInput at 232/264 and was
removed; the published pair contains no experimental production change.

The source call/move detectors conservatively treat a loop statement inside
`Expr.Block` as effectful. `proof_body_has_call` applies these detectors to the
assignment initializer, so branch joins cannot use their call-free retention
rule for this read-only search. A repair needs a bounded statement/expression
effect walk that includes block yields, nested assignments, calls, ownership
transfers, operator overrides and alias writes. Assignment-root collection must
also inspect block initializers before narrowing escaping-reference invalidation.
The entry replay walker separately needs to find this exact nested assignment
site without admitting stale post-loop initializer equalities.

## Bounded value-block effects and writes

Prover `6889cdda` classifies direct/parenthesized value blocks by walking their
statements and final expressions. Effect classification and assignment-root
collection fail closed at depth 64 or 4096 work units. Calls, transfers, parallel
loops, unsupported statements and source operator overrides remain effectful.
Other expression shapes keep the existing conservative call/move detection.
Nested block writes in declarations, assignments, conditions, iterables, match
guards, returns and contracts now enter the mutation set. Nested block writes
also disqualify extent retention conservatively.

Pure bodies that write only witnessed outer scalars do not exercise unrelated
escaped references; scoped local writes do not count as writes to the outer
frame. Reference/record writes and lends keep broader invalidation. Branch joins
can now retain prior guards across read-only loop-value assignments.

`test_value_block_effect_retention.py` passes six cases through both JSON routes:
conditional read and zero iterations accepted; nested/direct record writes,
mutating calls and insufficient entry bounds refused. Every case has zero
semantic errors, replay gaps and trusted assumptions. Existing loop-state,
invariant-retention and captured-search suites pass. The new suite is wired into
the full matrix. The original conditional scalar-invariant reproducer improves
to 7/8, zero findings, one nested entry replay gap (`block-effects-conditional.json`).
ActionInput remains 232/264, zero gaps (`block-effects-action-input.json`);
the uncached sweep remains 71/73 (`proof-block-effects-engine-sweep.log`).
Nested initializer authentication, snapshot bounds and full qualification remain
open. No engine runtime code changed in this slice.

## Nested assignment entry authenticated

Prover `167f3f1d` locates loop initializer entry inside nested plain-name
`<-` assignments. It walks statement scopes with depth 64/work 4096 limits,
requires one matching site, and retains exact declaration/initializer/invariant
positions and original-entry-only checks. An outer declaration of the same
name is not the scoped accumulator's reaching definition.

The original conditional reproducer now passes 8/8, zero gaps.
`test_captured_search_entry.py` passes ten cases through both JSON routes,
including conditional wrong initializer/preservation/bound refusals. Existing
initializer, identity-update and module-constant controls pass, including stale
exit and shadowed invariant claims. These tests remain in the full matrix.

Actual ActionInput remains 232/264, 32 findings, zero gaps/errors
(`nested-entry-action-input.json`). The uncached sweep remains 71/73
(`proof-nested-entry-engine-sweep.log`). The generic conditional-search defect
is repaired; this does not establish the actual capacity guard through all
ActionInput calls/joins or immutable copy bounds across mutation. Those and
full prover/shared/native qualification remain open.
