# Proofs alongside implementations

The plan now asks every slice to add or extend an implementation-linked proof
(contract items 7, 8 and 10). Prover holes are fixed in the engine's own prover
worktree, `../elisa-engine-proof` (branch `elisa-engine-proof` of
`elisa-proof`). `check` finds every proof in `proof/` instead of a fixed pair.

## Change

- `IMPLEMENTATION_PLAN.md`:
  - Contract item 7 names the prover worktree and requires an accepted and a
    rejected regression example per prover fix. It also requires merging the
    committed gains of `elisa-proof` `main` and `codex/wasmbrowser-proof`
    first.
  - Item 8 requires a proof per slice for its pure policy.
  - Item 10 requires the validation note to name the proof file, its
    obligation count and what remains unproved.
  - The queue and W10 point at these items.
- `scripts/check.elisascript`:
  - The default prover is `../elisa-engine-proof/build/elisa-proof`;
    `ELISA_PROOF_BIN` still overrides it.
  - The check lists `proof/*.elisa`, removes each stale report, runs each proof
    visibly, then writes its `--json` report to `build/NAME-proof.json`.
    Underscores in `NAME` become dashes, so `entity_id` writes
    `entity-id-proof.json`.
  - The paths are concatenated from bound `sview` locals. An f-string local
    with interpolation makes the ElisaScript 0.1 launcher trap (exit 133).
- `scripts/record_validation.py`:
  - `verified_proofs` requires a fully proved, replayed report for every proof
    source.
  - It fails when `proof/` has no proofs.
  - `test_record_validation.py` covers every-report, unproved, missing and
    empty cases.
- `test/check_workflow.py`, the shell-parity harness, is resynced with the
  check. It was last synced in 4ab8496 (2026-09-19). Later check stages were
  never mirrored, and its fake compiler read the output path positionally,
  which broke with the `-O0` world compile from 344651c. So every full-run case
  failed at 08e274a. The resync:
  - mirrors all current stages;
  - parses `-o` in the fake compiler;
  - adds fake `native_unit_tests.py` and `scene_file.py`;
  - checks the exact set of reports written, including the underscore naming.
- `README.md` and `docs/validation/native-gate.md` name the new prover path.

## Proofs

No proof changed in this slice. Prover 8ff46ae proves and independently
replays both existing proofs with 0 findings:

| Proof | Obligations |
|---|---|
| `proof/entity_id.elisa` | 17 |
| `proof/world.elisa` | 6 |

## Checks and commands

```
python3 scripts/test_record_validation.py
python3 test/check_workflow.py ~/.local/bin/elisascript
python3 scripts/check_source_length.py && python3 scripts/check_module_hygiene.py
ELISA_ALLOW_STALE_STAGE1=1 elisascript scripts/check.elisascript
```

Results on 2026-09-28:

- The recorder tests ran 8 tests: OK.
- The parity harness passed all 15 cases.
- The length and hygiene policies passed.
- The full check, run with `ELISA_STAGE1_BIN` pinned to a stage1 snapshot, exited 0
  with `failed: 0`. `build/validation.json` records `entity_id` (17 of 17
  replayed) and `world` (6 of 6).

## Negative control

- The parity harness catches a naming mutant. With the check's final report
  name changed from `name.replace("_", "-")` to `name`, the harness exited 1.
  The success case wrote `audio_virtual-proof.json` and
  `entity_id-proof.json` instead of the dashed names.
- The recorder tests reject an unproved report (4 of 5 obligations), a
  missing report and an empty `proof/`.

## Gaps

- The audio virtualization proof is not yet in `proof/`. It needs these
  prover holes fixed first: captured-loop exit invariants, short-circuit
  guards over pure calls, and fact propagation.
- The launcher's f-string trap is worked around here and is not yet reported
  to `elisa-script`.
