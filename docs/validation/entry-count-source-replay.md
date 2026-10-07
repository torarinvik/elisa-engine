# Unchanged entry-count source replay — 2026-10-08

Prover `95db5c6b`, immutable compiler `ddbc803d`: restores the original
`collection_push_count.elisa` to **20/20 obligations and certificates**, zero
gaps. The previous single gap belonged to `unchanged`, which returns `v.count`
without modifying the receiver and ensures equality to `old(v.count)`.

## Independent source authentication

The new replay helper requires a unique source owner, a primitive darray
parameter and a source ensure containing `old(receiver.count)`. It authenticates
the count expression's complete object span, one unambiguous synthetic symbol
definition, and absence of formal/local symbol collisions or receiver shadowing.
All executable statements must leave receiver and symbol untouched, including
borrowed aliases and calls. This does not keep the entry equation across mutation.

The route lives in the common local-binding liveness predicate, which both
certificate filtering and boundary validation use. An earlier candidate placed
it only at boundary validation; its source audit passed but certificate replay
filtered the binding out first. Retained diagnostic probe returned 0, identifying
that integration mismatch. No admission guard was removed.

## Acceptance and refusal controls

The complete push-count suite passes, including all existing wrong/missing/
conditional/user-defined pushes, stale counts, wrong values and mutation
refusals. Seven compiled provenance controls accept the readonly count and
refuse duplicate symbol definitions, a forged count equation with matching
re-encoded kernel mirror, push, pop, indexed write and borrowed alias mutation.
Kernel inventory: **10 tables / 199 entries match**. Uncached engine proof
sweep: **73/73**, status 0.

Logs under `build/validation`: `proof-entry-count-final-push-controls.log`,
`proof-entry-count-source-controls-retry.log`, `proof-entry-count-engine-sweep.log`,
`proof-entry-count-filter-build.log` and `proof-entry-count-clean-build.log`.
The terminal full matrix predates this repair; its remaining failure count,
shared/native qualification and broader implementation tasks remain open.
