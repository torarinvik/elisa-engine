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

## Captured initializer entry repair

Prover `78e9c1c1`/`ffd6bcd9` reconstructs the original captured-loop entry
inside a scalar initializer. It peels only empty capture-free parser wrappers
with a depth bound, then retains exact header initializer, invariant expression,
position, owner, environment and entry-only checks. Nonempty scopes and ambiguous
same-name declarations remain refused. Initial facts do not become exit facts.

Committed pair generation `91648f9978994d12b05a446624294f37` passes the four
new initializer controls on both JSON routes and all seven existing captured
entry controls. Valid entry proves completely; wrong initial value, invalid
update and stale exit equality refuse. The first rebuild exposed the parser's
empty wrapper and failed the control; qualification applies to the repaired
second build, not that earlier product.

The unchanged sound-event implementation now replays 151/157 certificates with
zero findings and six gaps. Remaining rows: number preservation, two read_fields
entry facts, number-call admission inside read_fields, and two dependent wrapper
summaries. Evidence: `build/validation/sound-assets-captured-initializer.json` and
`prover-captured-wrapper-build.log`. Full shared/native qualification stays open.

## Mutable captured-for entry facts

Prover `65f072ec`/`66fe2168` reconstructs exact original invariant entries for
mutable scalar locals captured by a top-level for statement. Each initializer
must be a literal or immutable primitive integer formal, with matching owner,
source declaration and trace positions. Between declaration and loop, only
independently checked integer declarations are allowed; calls, writes and unknown
statements refuse. Original invariant expression and all root position fields
must match. Loop-state premises cannot admit an entry equality. Body shadowing
of the target is refused. This adds no preservation or exit-state admission.

Pair generation `cf5a07edb0a64c34a4acff20d5e36346` passes six focused controls
on both report routes: valid entries, wrong initial count, body shadowing,
intervening write/call and stale exit. The minimal entry control still has
unrelated unsupported preservation bindings; its assertions qualify the two
entry certificates, not a complete function proof. Existing captured initializer
and seven captured-entry controls pass, as do malformed source admission on
all 12 routes and the kernel inventory.

The unchanged sound-event parser now replays 153/157 certificates, with zero
findings and four gaps: number preservation, the number call within read_fields,
and two dependent wrapper summaries. Evidence:
`build/validation/sound-assets-mutable-entry-final.json` and
`prover-mutable-entry-final-build.log`. Full qualification remains open.

## Saturating for preservation isolation

Two minimized reproductions are retained in the prover examples as
`nested_digit_loop_replay_gap.elisa` and `direct_digit_loop_replay_gap.elisa`.
Both retain four obligations: search has zero findings, but only three
certificates replay. The sole gap is invariant preservation after the
conditional accumulator assignment. These are open regressions, not passing
qualification evidence.

The direct-digit variant uses an immutable `u64` formal with `requires digit <= 9`;
it has no indexed reads, casts or nested digit declaration. It still fails,
so repairing indexed snapshots alone cannot close the parser preservation gap.
The nested-digit variant with the accumulator update replaced by `pass` replays
all four certificates. Production parser source and contracts remain unchanged.

An O0 diagnostic compiled with the installed compiler/runtime calls the actual
source-valid and fact-live predicates in preservation certificate context.
Both reject the nested digit equality and accumulator rebind equality. The
header initializer equality also refuses in this context, as expected for an
entry-only fact. Next repair: independently reconstruct original for-body
accumulator assignment, invariant and fresh-symbol identity; then admit stable
nested immutable declarations within that authenticated scope. Preserve
source positions, primitive-type/operator checks, and stale-state rejection.

Evidence: `build/validation/nested-trace-debug.txt`,
`nested-digit-loop-before.json`, `direct-digit-loop.json` and
`nested-digit-without-update.json`. This narrows the next implementation task;
it does not qualify the full prover matrix or engine gate.
