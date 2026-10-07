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
