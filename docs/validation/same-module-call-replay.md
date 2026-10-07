# Same-module call replay — 2026-10-08

## Defect and repair

After signed product growth verified `Retime::step`, the unchanged dependency-row
probe reached a replay gap in `range_step`. The producer qualifies repeated
bare callees (`step` becomes `Retime::step`), while replay compared that spelling
to the original bare source call. Diagnostic qualification of the call, or
renaming the unrelated `Contact::step`, removed the gap.

Prover commit `79155ac4` reconstructs a bare spelling only when source declares
one target in the caller's exact module and no parameter/local shadows that
name. It changes neither actual arguments nor labels nor source positions.
Both the summary's raw source-site check and the deterministic marker's existing
state reconstruction use this source-authenticated alternative. Wrong owners,
ambiguous declarations and callback shadows cannot use it. Calls in enclosing
modules are not claimed by this same-module fallback.

## Acceptance

- Original `test_replay_dependency_row.py` now passes. The unchanged source has
  **20 obligations, 18 proven, 18/18 certificates replayed and zero gaps**.
  Both remaining findings are the expected unproven preconditions in the
  invalid `Zed::apply`; no contract or failing row was removed. Artifact:
  `build/validation/proof-same-module-dependency-row.json`.
- `test_same_module_call_replay.py`: full JSON accepts bare and qualified
  same-module calls and refuses a different module and changed actual.
  Focused JSON explicitly retains the pre-existing unverified module-callee
  refusal; all cases have zero semantic errors and replay gaps. Focused
  dependency qualification is still open.
- Existing compiled rebind/alias-forgery controls pass: misplaced, colliding,
  cyclic, stale, misattributed, branch-only and loop-rewritten traces refuse.
  Log: `build/validation/proof-same-module-forgery-controls.log`.
- Signed product growth and partial-literal-requires controls still pass.
- Uncached engine sweep: **73 total, zero cached, status 0**. Log:
  `build/validation/proof-same-module-engine-sweep.log`.
- Source length and diff policy checks pass.

Clean ddbc803d paired generation: `d359f0e1eebb440ea581823f9d7893d8`.
Build log: `build/validation/proof-same-module-clean-build.log`.

## Remaining gates

A fresh full matrix is running on committed 79155ac4 with explicit immutable
compiler root/binary/runtime/revision and two report workers. Log:
`build/validation/proof-79155ac4-ddbc803d-full-matrix.log`.
Its terminal result is pending; retain the previous 73-step failure result as
historical. Shared/native gates and broader focused-module support remain open.

The smaller literal-postcondition fixture also exposed a separate source
inventory refusal despite complete certificate replay (`source-obligation-inventory`:
missing distinct proof attempt). The focused resolver fixture uses distinct
symbolic postconditions to isolate this repair. That inventory issue remains
unqualified and must not be conflated with call-spelling replay.
