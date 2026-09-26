# Aggregate reference escape diagnostics

Elisa-compiler commit `9d115bac` extends `Semantic::check_borrow_after_move` to
report local storage that escapes in reference-bearing result types. It checks
direct aggregate literals and returned fields, tracks reference-bearing local
aggregates through branches, and follows uniquely named helper chains with one
reference parameter when the helper places that parameter into its result.
Caller-owned reference parameters remain accepted.

The regression fixtures live in
`../Elisa-compiler/test/fixtures/diagnostics/aggregate_reference_*.elisa` and
are registered in the diagnostic smoke harness. On macOS 27.0 / Apple M5 the
compiler seed succeeded, then the following checks passed:

```
cd ../Elisa-compiler
DEVELOPER_DIR=/Library/Developer/CommandLineTools \
ELISA_STAGE1_SEED_OPT_LEVEL=-O0 bash scripts/elisac_stage1.sh --seed
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash test/parity/diagnostics_smoke.sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash test/parity/contextual_ownership_smoke.sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash test/parity/reference_reborrow_smoke.sh
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash test/parity/region_scope_smoke.sh
```

Results: the diagnostic harness passed 424/424 fixtures; contextual ownership,
reference reborrow (27 rejected contexts), and region-scope smokes passed. The
seed rebuilt the compiler and runtime object. `-O0` was used to shorten the
self-host build; this validates semantics, not optimized compiler performance.

This closes the local-reference-in-returned-aggregate gap reproduced with a
`BorrowedBox{value: pack(&local)}` result. It does not complete phase-borrow lifetime
analysis: overloaded lender names, multiple-reference-parameter relationships,
and general alias flow remain outside this checker. Those cases must not be
treated as proven safe; W03 remains open until the broader model and driver
acceptance smoke pass.
