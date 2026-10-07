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
