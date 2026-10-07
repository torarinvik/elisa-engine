# Ordinary enum resource summaries — 2026-10-08

## Defect and repair

ActionInput context had 264/265 replayed obligations, with one
`borrow-call-summary-unsupported` finding in `bind`. A minimal mutable-reference
callee returning a payload-free ordinary enum reproduced the refusal (1/2);
returning bool or a const enum passed (2/2).

Prover commit `cb316eaa` extends the reference-free storage audit to uniquely
resolved ordinary enums. Every variant payload is recursively audited, including
struct fields and container elements. The source enum annotations travel through
the audit so hierarchy/common-field enums fail closed. Ambiguous names, recursive
payloads, reference payloads and unknown shapes remain refused. The existing
borrow source compatibility, overlap and region mapping checks are preserved.
Production ActionInput source and contracts are unchanged.

## Acceptance

Compiler product remains the immutable `8006b660` snapshot described in
[installation evidence](compiler-8006b660-install.md). The newest guide review
at `ddbc803d` is separate from compiler product qualification.

- `python3.14 ../elisa-engine-proof/scripts/test_reference_free_enum_summary.py`:
  seven cases pass both `--json` and `--function-json`: plain enum, scalar payload
  and enum-valued parameter field accepted; reference payload, nested reference
  payload, recursive payload and hierarchy refused. All have zero semantic errors,
  replay gaps and trusted assumptions.
- Existing `test_plain_enum_resource_call.py` retains ambiguous and overlapping
  refusals; `test_plain_enum_propositions.py`, `test_required_reference_store.py`
  and `test_mutable_local_resources.py` pass.
- Prior `test_private_value_declaration.py`, `test_conditional_fixed_extent.py`,
  `test_branch_local_loop_atom.py`, `test_record_branch_state.py` and
  `test_old_mutable_reference.py` pass, including invalid mutation/stale-state cases.
- Actual `--json proof/action_input_context.elisa`: **265/265**, no findings,
  zero semantic errors and **265/265 certificates replayed, zero gaps**.
  Artifact: `build/validation/input-reference-free-enum-context.json`.
- `python3.14 scripts/prove_all.py ../elisa-engine-proof/build/elisa-proof --no-cache -j2`:
  **73 total, zero cached, 73 run, status 0**, including both ActionInput reports.
  Log: `build/validation/proof-reference-free-enum-engine-sweep.log`.

Clean paired generation `fd5479080fde4b70bd42399c66fbc615` names the committed
prover source. The repeated uncached sweep on this clean pair also passes 73/73;
logs: `build/validation/proof-reference-free-enum-clean-build.log` and
`build/validation/proof-reference-free-enum-clean-engine-sweep.log`.

## Remaining qualification

The full prover compatibility matrix previously had 73 failed steps and needs
fresh triage and completion. Shared/native gates and qualification of the new
compiler product remain open. The independent scalar-copy snapshot reproducer
still lacks source pre-state authentication across record mutation. A successful
engine sweep does not establish those broader requirements.
