# Full prover matrix — 2026-10-08

## Terminal result

Ran the complete `scripts/test.sh` with `KEEP_GOING=1`, prebuilt paired prover
`cb316eaa`, immutable compiler `8006b660`, matching runtime and parser snapshot,
`ELISA_PROOF_SKIP_BUILD=1` and two report workers. The owned run finished
**status 1, 73 failed steps**. All checks were retained. Log:
`build/validation/proof-cb316eaa-full-matrix.log`; structured step records:
`build/validation/proof-cb316eaa-full-matrix-summary.json`.

This contradicts full prover compatibility qualification even though all 73
engine reports pass. Failures include summary dependency replay, collection and
counting-loop state, converted indices, conditional joins and compiled harness
controls. The deterministic loop-call replay and rebind/alias-forgery harnesses
pass. Do not remove failing cases or raise expected baselines.

## Harness repairs

- Split the 613-line setup part into setup/Python suites and ordered compiler
  probes, now 346 and 269 lines. A byte comparison confirms concatenation
  preserves every original line; the added include regression is the only new
  step. Sorted execution places compiler probes before kernel audit.
- Temporary public copies of private source-binding helpers now resolve include
  paths against the original helper directory. The new immutable-binding helper
  includes `value_block_immutable_bindings.elisa`; relocation previously left
  that relative path pointing into scratch storage.
- `test_source_binding_harness_includes.py`, shell syntax, source-length policy
  and diff checks pass. Production helper visibility is unchanged.
- Focused `test_loop_invariants_compile.py` with explicit immutable compiler
  root now compiles and executes the source-binding harness, then returns
  **128**, the binding-sink matrix refusal control. That is an unresolved replay
  control failure, not successful qualification. Log:
  `build/validation/proof-source-binding-harness-include-repair.log`.

## Next repairs

First isolate which binding-sink control returns 128 and establish whether it
accepts an invalid source binding. Retain all sink, overloaded operator/cast,
shadow and forged-position controls. Separately, the dependency-row probe has
14 certificates, 13 replayed and one gap in a literal call-argument binding;
artifact: `build/validation/proof-cb316eaa-dependency-row.json`. Then repair the
remaining complete-matrix failures and qualify a paired prover on the newest
compiler product. Fresh full matrix/shared/native gates remain open.

## Binding-sink qualification repair

Prover test commit `f3ee9522` assigns distinct codes to the sink checks. The
failure was the exact indexed-read initializer (`sink_indexed_read`, code 131).
The production indexed-snapshot source auditor already accepts its equation
when the entire source suffix is read-only; expecting it to be unsupported was
obsolete. The source reconstruction checks declaration/type/position and audits
the suffix, rather than assuming an arbitrary indexed equation.

Changed this one expectation to acceptance and added three refused source
controls: mutation of the array element, rebinding the index, and a later call.
All assignment, record-field, indexed-write, call, nested-call, branch-join and
return sink controls remain. The complete compiled source-binding harness now
passes with explicit immutable 8006 compiler root; the loop fixture is proved
with zero replay gaps. Log: `build/validation/proof-binding-sink-controls.log`.
Source length and diff policy checks pass (harness 594 lines). No production
replay predicate changed in this correction. Full matrix qualification remains
open; the original 73-step failure result is retained above.
