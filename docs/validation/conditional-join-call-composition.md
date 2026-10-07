# Conditional join call composition — 2026-10-08

## Retained production failure

The full matrix on prover `79155ac4` and immutable compiler `ddbc803d`
reports conditional_join replay coverage failure. An independent JSON run of
`../elisa-engine-proof/examples/conditional_join.elisa` reproduces 22
obligations, 20 proven and 22 certificates with 20 replayed (two gaps).
There are no findings or semantic errors. Both gaps are `walk` loop-invariant
certificates at line 45; their contexts include the `step(t)` summary, the
`finish(n, end)` deterministic call and the branch-join facts.

## Diagnostic isolation

Kept the original fixture unchanged. Three ignored diagnostic copies use the
same paired prover binary:

| Diagnostic change | Obligations/proven | Replay gaps |
| --- | --- | --- |
| Fully qualify both calls | 22/20 | 2 |
| Replace only `finish(n, end)` with `n` | 18/18 | 0 |
| Replace only `step(t)` with literal `1` | 20/20 | 0 |

These comparisons isolate composition of the two calls through the loop join,
rather than same-module spelling. They do not establish that either substituted
program covers the original behavior. The original remains a required acceptance
fixture, and source, state, branch and stale-value refusals must remain intact
when repairing replay. Production prover source was not changed during the
live full matrix.

JSON artifacts: `build/validation/conditional-join-ddbc.json` and
`conditional-join-{qualified,no_finish,no_step}.elisa.json` in the same directory.

## Qualified-call harness repair

Separately, the matrix's qualified-call harness exit 23 selected the last
`caller_local` summary trace, which binds `result` to `result_value`, and passed
that identifier as an expected call. The earlier summary binds `result` to the
actual call. Prover test commit `88b1f7d6` selects that first trace. The complete
focused harness now passes, including wrong-owner, argument, span, reassignment
and forged-binding refusals. Log:
`build/validation/proof-qualified-call-harness-selection.log`. No production
proof rule was relaxed. The live full matrix began before this test repair.

## Terminal matrix and first rejected trace

The owned full matrix terminated with status 1 and **72 failed steps**.
Retained log: `build/validation/proof-79155ac4-ddbc803d-full-matrix.log`;
structured failure contexts: the adjacent `-summary.json`. This run began
before the qualified-call harness correction; that one failure now has separate
passing focused evidence, and the remaining total has not been rerun.

A source-level diagnostic executable, compiled with the immutable ddbc803d
product, parsed the original fixture and checked each `walk` trace with owner
line 41 and consumer line 45. It returned 53, identifying zero-based trace 52,
a function summary at line 48 restated over local `d`. The source lifetime
validator rejects consumers before their binding line. The loop-invariant
certificate uses header line 45 although its body binds `d` at line 48. This
audit result identifies the first rejected trace in that context; it does not
yet prove the full certificate's rejection path or authorize relaxing the
consumer-order guard. A repair must authenticate the particular loop-body
exit context and retain pre-loop, stale, shadowed and branch-only refusals.

Diagnostic source/build log/executable:
`build/validation/conditional-join-trace-probe{.elisa,-build.log,}`.
