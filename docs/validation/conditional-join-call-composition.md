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

## Loop-body consumer experiment

Built a candidate that audited body-local aliases at the body end only when
the exact consuming certificate contained source-authenticated guard and
invariant facts for that loop header. The paired strict O2 build succeeded;
the original fixture still reported 22 obligations, 20 proven and two gaps.
Removed the candidate production change. This experiment does not establish
a sufficient repair: branch-scoped summaries feeding the guarded join also
need their own authenticated consuming context. Do not bypass lexical lifetime
or let a branch-only value reach an unconditional consumer.

Retained build log: `build/validation/proof-loop-alias-exit-build.log`;
report: `build/validation/conditional-join-loop-exit-candidate.json`. The build
products from this uncommitted experiment must be refreshed from restored
source before subsequent acceptance runs.

## Exact certificate-context audit

Refined the trace probe to set the one-based consumer certificate index to 21
and iterate only certificate 20's cached fact origins. On restored committed
source it returns 66, identifying zero-based trace 65, a branch-join fact at
line 50 with one premise. This corrects the earlier all-traces audit's scope:
trace 52 was the first failure in that scan, while trace 65 is the first
rejected fact in this particular certificate's ordered context. Recursive
summary/precondition validation must be traced before choosing a repair.
The `finish` summaries bind their results directly to call expressions, not
local aliases; the failed experiment does not establish a branch-local alias
as the cause.

Two additional ignored diagnostic copies retain both calls. Inlining `step(t)`
into `n`, or putting it in a value-block initializer for `d`, each proves all
22 obligations with zero gaps. The original fixture is unchanged. These
comparisons narrow the producer/provenance interaction but do not close it.
Reports: `conditional-join-{inline-step,step-result-block}.elisa.json`.
Exact-context probe: `conditional-join-certificate-trace-probe` and its source
and build log, all under `build/validation`. Restored paired build completed
as generation `d7ff40e6f50e467b9889d8050ec34384`.
