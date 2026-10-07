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

## Actual read-only call boundary isolated

`scripts/diagnose_action_input_guard.py` runs four source variants against
`bind_checked` without editing production source. The original has 37/41;
removing only the conditional search gives 32/36. Replacing `state_slot` with
literal zero gives 29/32, and replacing the call plus removing the search gives
24/27. All have zero gaps/errors; the remaining three findings are later slot
upper bounds. Log: `action-input-call-guard-diagnostic.log`. These intentionally
failing diagnostic cases are not an acceptance gate.

Prover `f93e1be0` adds `test/repro/read_only_search_call_guard.elisa`: the
read-only search helper verifies, but its caller loses the field guard (9/10,
one index-upper finding, zero gaps). Replacing only the helper body with a
literal result gives 5/5, zero gaps. Reports: `read-only-search-call-guard.json`
and `read-only-search-call-literal.json`.

The direct-purity checker refuses all loop statements and mutations, including
scoped accumulator updates, so the query uses ordinary call havoc. Preserves-only
frames currently do not restore caller facts either. Next establish an independently
source-authenticated read-only call boundary for bounded searches, with mutable
callee/global/callback/alias controls. Do not classify arbitrary loops as pure or
rewrite the production search into an unrolled/literal helper. Later slot bounds
and immutable snapshots remain separate tasks.

## Finite read-only search call classified

Prover `6732bf6b` recognizes a finite exclusive-range search with a fresh
unsigned accumulator and no capture write-back. The only allowed writes assign
the range binder to that accumulator; conditions/invariants must be supported
read expressions without calls, transfers, overloaded operators or mutable
global reads. Mutable parameters, effects and frame-changing contracts retain
their existing exclusions. Nested search-body checks have depth/work limits.
The existing verified-callee and purity dependency checks remain required.

The minimal call-boundary reproducer now passes 10/10, zero gaps. The six-case
suite accepts read-only and zero-iteration queries and rejects mutable callees,
global reads, effectful callbacks and insufficient caller guards. Both JSON
routes run. All cases have zero semantic errors/trusted assumptions; the rejected
effectful callback retains one explicitly asserted preservation replay gap.
That gap remains a qualification defect, not a passing replay claim. Existing
pure-contract, captured-search and loop-state controls pass.

Actual context improves to 233/264, 31 findings, zero gaps/errors
(`pure-search-action-input.json`). `bind_checked` improves to 38/41: the binding
slot upper bound now proves, leaving three later state-slot bounds. All four
source variants assert the binding-slot goal is proven
(`action-input-pure-search-guard-diagnostic.log`). The full uncached engine sweep
remains 71/73 (`proof-pure-search-engine-sweep.log`). No engine runtime change.
Immutable snapshots, later slot bounds, the callback preservation gap and full
matrix/shared/native qualification remain open.

## Region exit diagnostic — 2026-10-07

An uncommitted prover candidate extends call-stable fact restoration to region
blocks that declare locals. Build generation `5cf3351e5732499d9f56e41b89ca46b8`
finished using the immutable compiler 8006 product. The actual `bind_checked`
function reports 41/41 with zero replay gaps; the context reports 236/263,
27 findings and zero gaps. The changed obligation total is retained explicitly.
These candidate results do not establish full compatibility or acceptance.

A minimal guarded outer scalar copy survives a region containing a fresh local
and a literal-index aggregate write: 5/5, zero gaps. The insufficient-entry guard
is refused with one finding and zero gaps; a same-name region shadow is also
refused. The immutable-rebinding probe has a semantic error and is not a valid
mutation control; rerun with a mutable declaration before accepting the repair.
An inner-local-index variant has one index-upper replay gap, even though the
post-region outer index replays. This is a separate source binding path to repair,
not evidence that region-local indexing is qualified. Candidate logs and JSON
are under ignored `build/validation/region-*` and `input-region-*` paths.

### Qualified region retention slice

Prover commit `3ed40ddd` additionally excludes every fact mentioning a region
local before applying the scalar stability filter, including shadowed spellings.
`test_region_outer_facts.py` is wired into the matrix: untouched and declaration-only
cases prove through both JSON routes; insufficient guards, valid mutable rebinding,
mutating calls and shadowing refuse with zero gaps and no semantic errors. Existing
invariant-for and pure-search suites also pass. The filtered build retains actual
bind_checked 41/41 and context 236/263, 27 findings, zero gaps. Its uncached engine
sweep remains 71/73, failing only the two ActionInput reports. Full matrix remains
unqualified. The inner-local-index replay gap above remains open independently.

### Post-repair diagnostic boundaries

The committed `diagnose_action_input_guard.py` now records the changed outcomes
rather than asserting obsolete three-finding counts for every variant. Production
bind_checked is 41/41; literal-call is 32/32, both zero gaps. Removing the search
leaves 33/36 and three findings. Combining search removal with a literal slot
produces 24/27 and three replay gaps, zero producer findings. These remain
explicit diagnostic failures, not passing acceptance cases. Region retention
therefore does not qualify arbitrary field-copy snapshots or altered search paths.

The scalar-field-snapshot diagnostic still reports 4/5 for the field copy, 5/5
for local-guard and parameter controls; stale fields, rebound copies and wrong
entry bounds are refused with their original counts and zero gaps. A separate
minimal captured-for loop with a fixed flags array and a write between two reads
proves 7/7 with zero gaps (capture-free and literal-write variants also 7/7;
no-write 5/5). Capture syntax or an indexed write alone does not reproduce the
remaining actual apply failures. Continue isolating its aggregate bindings,
conditional field updates and callee effects without changing production shape.

## Conditional record update loses fixed extent

The minimal `test/repro/conditional_record_write_fixed_extent.elisa` in the prover
reproduces an actual apply failure: inside a captured bounded loop, conditionally
write Store.active, then read Store.flags[index]. It reports 7/8, one
index-bounds-opaque finding, zero gaps and zero semantic errors. Removing the
capture does not change this result. The earlier unconditional-write minimal
case passes 7/7. The branch join deliberately invalidates a parameter's post-state
when arms disagree (`proof_restore_branch_values`), to avoid turning a mutated
field's equality to its old value into a reflexive proof. Recovering fixed array
extent must preserve that mutation refusal rather than restoring the stale root.

Actual apply source variants corroborate this path: original 63/89 with 26
findings; remove last_active_device write 71/93 with 22 findings and no opaque
bounds. Replacing state_slot with zero leaves 54/80 and the same 26 findings;
removing refresh_action leaves 30/56 and the same 26. All have zero replay gaps.
Removing loop captures produces 46/47 with a proposition-type finding, and is
not an accepted production rewrite. Evidence is in ignored apply-*.json files.

### Safety boundary identified

The opaque receiver originates before extent lookup: `proof_check_index_safety`
in statement_checks.elisa substitutes the whole source expression, and
`proof_restore_branch_values` has intentionally set the disagreeing mutable
parameter root to Invalid. Thus Store.flags becomes Field(Invalid, flags).
`proof_check_index_access` rejects that receiver before consulting fixed_names.
The declared fixed extent is still available but source place identity has been
lost. This is not an absent arithmetic bound or a callee summary failure.

The repair belongs at the index-safety substitution boundary: preserve a
source-authenticated fixed-array place solely for its extent, while substituting
index expressions and dynamic-count receivers with the current state. Ordinary
expression evaluation and contracts must continue seeing Invalid for the
mutated record; restoring its old identifier at the branch join would revive
stale field facts. Qualify direct and nested indexes, wrong bounds, shadowed
places, dynamic arrays and changed-field old-value contracts before acceptance.
