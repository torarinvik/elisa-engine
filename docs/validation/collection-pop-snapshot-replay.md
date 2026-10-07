# Collection pop snapshot replay — 2026-10-08

## Original failure

Prover `725ee3b1`, immutable compiler `ddbc803d`: the original
`collection_pop_value.elisa` emits eight certificates but replays six.
`take_last` and `take_two` have gaps at return lines 6 and 13. No findings
or semantic errors are reported. The complete fixture remains unchanged.
Report: `build/validation/collection-pop-725e.json`.

## Exact consuming-context audit

A source-level probe parses the original fixture, runs producer and replay,
and audits certificate 1's cached fact origins with owner line 2, consumer
line 6 and one-based consumer certificate index 2. Exit **15** identifies
zero-based trace 14, the first derived `proof-step` at pop line 5.
Its conclusion is `x == 9`; its fourteen premises include trace 13, the
synthetic local binding `x == v[v.count - 1]`. The original source initializer
is `v.pop()`, not that indexed expression.

The producer in `check/returns/declarations.elisa` models the value pop as
a last-slot read and then removal. `check/collection_pop.elisa` derives facts
about captured `x` before forgetting the receiver. Replay's indexed-snapshot
source route requires a matching indexed initializer and a readonly suffix,
so it cannot directly authorize this synthetic pre-pop equation. Allowing
that equation as an ordinary post-pop fact would confuse two array states.

The repair must authenticate the source builtin pop, receiver/element type,
exact binding and its pre-state derivation, and export only stable captured
scalar consequences. Preserve rejection of first-slot-as-last claims, stale
receiver facts, changed counts, writes to the local and user-defined methods.
No production replay change has been made in this diagnosis.

Probe source, executable and build log: `collection-pop-trace-probe.elisa`,
`collection-pop-trace-probe` and `collection-pop-trace-probe-build.log`, all
under `build/validation`.

## Repair accepted — prover bf0b403e

The producer now records a literal captured pop value as `collection-pop-value`.
Replay independently reconstructs a unique source owner and exact complete
binding span, matching primitive darray element/local types and mutable-reference
receiver. It requires a source nonempty guard and last-slot literal equality,
refuses user functions named `pop`, unsupported scopes/imports and earlier
non-contract statements, and permits only later same-receiver pops and return
of the immutable capture. It never admits the temporary indexed equation as
a post-pop fact. Count-only derivations omit premises naming the captured local.
Other nonliteral candidate shapes retain their derivation path; this source
route currently covers literal last-slot contracts with the audited body shape.

- Original fixture: **8/8 certificates replay**, zero gaps.
- Existing five rejected value/count claims: refused with zero replay gaps.
- Compiled source controls: 13 cases, including wrong literal/local, start and
  end span shifts, first-slot substitution, local write, prior pop, user pop
  function, owned receiver, missing guard and element mismatch refusals; honest
  captures and a later same-receiver pop accepted.
- Uncached engine sweep: **73/73**, status 0 on final products.
- Kernel inventory: **10 tables, 199 entries match source**. Added the new
  boundary kind and documented four existing syntax/width helper dependencies
  after inspecting their implementations; no checker predicate changed there.

Logs under `build/validation`: `proof-pop-snapshot-final-regressions.log`,
`proof-pop-snapshot-builtin-controls.log`, `proof-pop-snapshot-final-engine-sweep.log`,
`proof-pop-snapshot-builtin-build.log` and `proof-pop-snapshot-clean-build.log`.
Initial backend match declines are retained in build/retry logs; small helpers
compile without dropping checks. Full matrix/shared/native qualification remains
open and the terminal 72-failure matrix predates this repair.
