# Literal argument replay before a refused call — 2026-10-08

## Defect

The full matrix dependency-row probe reproduced on both compiler 8006 and
new compiler ddbc803d: 19 obligations, 13 proven, 14 certificates but only 13
replayed. The gap was the proven literal weight precondition in `Zed::apply`,
which calls `Gate::apply(x, x, 0)` without establishing the bounds on `x`.
The call remains invalid. Its independently proven weight obligation should
still replay.

The producer binds the source literal to a typed fresh symbol before checking
requires. If any requires fails, no postcondition summary is emitted. The
literal-binding validator relied on such a summary, so it refused the honest
binding solely because another precondition failed.

## Repair and boundaries

Prover commit `c867efd1` independently reconstructs a direct source call at the
binding's exact span in its uniquely identified owner. It resolves the callee
module and declaration row, matches exactly one positional literal argument to
its formal, checks signed primitive type/range and source positions, and retains
the existing fresh-symbol uniqueness checks. It derives no postcondition and
assumes no precondition. Named/default or nested/scoped call shapes remain on
the existing summary-backed path; this fallback does not claim support for them.

## Acceptance

- `test_literal_call_partial_requires.py`: three cases pass both JSON routes:
  partial preconditions, signed negative literal and declaration initializer.
  Calls remain refused with zero semantic errors, replay gaps or trusted
  assumptions. Log: `build/validation/proof-literal-source-focused.log`.
- Actual dependency-row probe: **19 obligations, 14 proven; 14/14 certificates
  replayed, zero gaps**. Its five failed/unproven obligations remain. Artifact:
  `build/validation/proof-literal-source-dependency-row.json`.
- Compiled `tests/test_replay_rebind_and_alias_holes.py`: colliding, misplaced,
  cyclic, stale, misattributed, branch-only and loop-rewritten forgeries refuse;
  honest bindings replay. Log:
  `build/validation/proof-literal-source-forgery-controls.log`.
- Uncached engine sweep: **73 total, zero cached, status 0**. Log:
  `build/validation/proof-literal-source-engine-sweep.log`.
- Source-length and diff policy checks pass.

Compiler product is immutable ddbc803d with the recorded matching runtime and
parser snapshot. Clean committed-source pair:
`9d733b39c4e44d1c87f487e8a4d98f63`; build log:
`build/validation/proof-literal-source-clean-build.log`. The repeated uncached
sweep on this clean pair also passes all 73 reports; log:
`build/validation/proof-literal-source-clean-engine-sweep.log`.

Full matrix qualification is open. The dependency-row regression still needs
its bounded multiplication/callee resolution obligations repaired; the retained
full matrix had 73 failed steps. This slice closes the literal binding replay
gap, rather than establishing that entire regression or gate as passing.
