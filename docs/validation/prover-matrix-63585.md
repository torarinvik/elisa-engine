# Exploratory prover matrix — compiler 63585c5f

The full KEEP_GOING run reached terminal exit 1 with 82 failed steps.
Log: `build/validation/prover-full-matrix-63585-current.log`.
This is an exploratory census, not qualification: several test expectations
were repaired and committed while the run was active. Production prover sources
remained frozen until the process reached terminal.

Repairs now committed in the prover checkout:

- Source-authenticated call aliases: accepted cases require complete replay;
  wrong result, wider domain and reassignment controls still refuse.
- Corpus manifest: source identity now includes the committed diagnostic
  assertion update, with workload outcomes preserved.
- Kernel inventory: records the fail-closed call scan used by captured entry
  source validation.
- Open proof rendering: test requires the nonzero focused route exit and keeps
  forged and foreign proof-block rejection.
- Stable conditional initializer predicates: source validation now recursively
  checks conjunction, disjunction and negation over stable integer comparisons.
  Six controls pass on both report routes in an isolated strict-O2 product pair;
  the digit_step source/kernel replay gap closes. This preserves immutable-input,
  source-position and overloaded-operator checks. The committed strict-O2 rebuild
  passes all six controls on both routes; a fresh engine sweep remains 69/73.

High ROI next repairs:

1. Decimal saturation: direct value/digit parameters bounded by CAP and 9 prove;
   the parser-derived bound still does not close preservation. The negated-order
   integer-term classifier refused the closed literal quotient CAP / 10.
   Prover `390f9566` plus controls `4b8929db` now independently classify a
   nonnegative literal numerator divided by a positive literal denominator in
   producer and replay. Large/small saturation cases pass both report routes;
   wrong threshold and zero divisor refuse. Existing denial and predicate tests
   pass. Sound-event search now has zero findings, but replay is only 150/157.
   Preserve
   the original implementation and contract while repairing the seven source
   replay gaps. Pair generation: `3af1e2b2fdf6416092fccbaf558111a3`.
2. Captured and mutable loop entry/exit source validation: loop_state_joins has
   nine replay gaps after the predicate repair; decimal preservation remains
   unproven. Entry equalities must never survive a loop update as exit facts.
3. Remaining source replay and qualified runtime harness failures, including
   source-binding runtime exit 128 and deterministic call harness exit 23.
   Reproduce under an exact compiler/runtime pair before changing expectations.
4. Separate compiler semantic diagnostic changes, inventory/CLI changes and
   actual proof gaps; do not weaken expected rejection or full replay assertions.

After repairs, run the matrix on a clean frozen revision and repeat the uncached
73-row engine corpus, shared gate and native gate. Hosted pins and packaging
qualification remain downstream requirements.
