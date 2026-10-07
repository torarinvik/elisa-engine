# Compiler 50b16e68 installed; engine qualification pending

The UI chat installed upstream revision
`50b16e6800fe03e3369a78aa85fffb55ee2f38e3` on 2026-10-07. The engine chat
independently read the installed SNAPSHOT and ran its source freshness check:

```
DEVELOPER_DIR=/Library/Developer/CommandLineTools bash /Users/torarinvikbjarko/.elisac/stage1/scripts/assert_stage1_fresh.sh /Users/torarinvikbjarko/.elisac/stage1/bin/elisac-stage1
```

Exit zero. SHA256 of installed compiler:
`a0a9b2951d68c64f3a3c56cf20ff99eee8256af3f0ff467c591cecaac6eb1bae`.
Runtime: `ca40ba1db8a74110936ad5cdaf808707020c5c74ebb6e491bda2198696d13b8a`.
This revision adds audited borrow-exclusivity checks and reference-field
backend lowering. Its engine performance has not been measured.

The mocap branch bridge commit `3c51102b` passes strict extern declaration
compilation for five bounded FBX entry points, plus one linked call at O0/O2
on this compiler/runtime pair. Native boundary, staging, actual conversion,
digest/cache and source/existing-output preservation checks pass. Exact
header and evidence are in that branch's `native/studio_fbx_bounded.h` and
`docs/validation/fbx-bounded-bridge.md`; client staging/cache remains there.

This focused check does not establish shared/native engine gates, a refreshed
prover product pair or the full prover matrix under 50b16e68. Those must be
rerun with matching archived parser/runtime inputs. Earlier 63585c5f evidence
remains historical qualification of that specific pair, not this installation.
Hosted pins remain unchanged pending the complete gates.

## Engine qualification results

Strict O2 prover pair generation `de12689f110f42228c452db5c13a2cef` built
from prover `bd2e036b`, using installed 50b16e68 compiler/runtime and matching
archived compiler/parser sources. Both products completed with frozen sources.
Build log: `build/validation/prover-50b16e68-build.log`.

Fresh engine tests: 214/214, zero compile-cache hits, four workers, 44 seconds.
`build/validation/engine-tests-50b16e68.log`. This is compatibility evidence,
not a controlled performance comparison with the previous compiler. Native
unit tests also pass (`native-units-50b16e68.log`).

Focused prover checks pass: seven decimal-for saturation controls, four
captured initializer controls, six mutable-for entry controls, seven existing
captured entry controls, seven source-call alias controls, six stable predicate
controls, four literal-quotient controls, malformed source admission (six classes
on all twelve routes), and kernel inventory (10 tables /194 entries).

Uncached engine corpus: 73 run, 69 pass, four fail.
`build/validation/proof-sweep-50b16e68.log`. Exact outstanding reports:

| Proof | Proven / obligations | Replay gaps | Findings |
| --- | --- | --- | --- |
| action_input_context | 31/74 | 0 | 44 |
| action_input_deadzone | 31/74 | 0 | 44 |
| audio_anim_events | 45/58 | 0 | 13 |
| sound_event_assets | 153/157 | 4 | 0 |

The two ActionInput reports additionally contain four semantic diagnostics,
but zero semantic errors. Producer findings and replay gaps are distinct: the
first three failures need proof coverage, whereas sound assets needs source
replay reconstruction. None of these failures was waived or removed.

Full shared command used `elisascript scripts/check.elisascript`, explicit
installed compiler/matching prover, Python3.14, CommandLineTools, Wicked root
and forced audio-device-unavailable policy. Terminal exit1 at the proof stage.
All earlier stages pass, including cached rerun of214 tests, SDL3, Godot
compatibility, native units, scene/skin/localization artifact checks, Metal
viewport render/motion, navigation and owner/privacy rejection fixtures.
Log: `build/validation/shared-check-50b16e68.log`.

The complete prover regression matrix, full native application gate, refreshed
package and hosted execution still remain open. Shared gate remains red at
proofs; focused compatibility does not promote hosted pins.

## Record-array source repair

Prover `72806cfb` extends fixed-array shape, element and source proposition
projection to a uniquely declared concrete record head, retaining alias/name
ambiguity rejection and checked constant extents. Nested module names use the
parser's qualified suffix spelling; multiple matching constants still refuse.
Pair generation `48f775f6fa364597a914a6e6c74970b9` passes eight controls on
both JSON routes: named/literal/nested/private/mutable arrays, wrong bound,
wrong result and ambiguous extent. Literal-index controls and kernel inventory
pass. Build: `build/validation/prover-record-array-final-build.log`.

ActionInput moves from31/74 proved obligations to89/185, all89 certificates
replayed. The original source and contracts are unchanged. Newly checked
index accesses expose additional bounds and call-summary failures;100 findings
remain, so this is restored coverage, not a passing input proof. Audio animation
remains45/58 with13 findings. Fresh engine sweep still69/73, same four files;
`build/validation/proof-record-array-sweep.log`. Full gates remain open.

Two existing regressions remain red before/after this change, with identical
reports: fixed_array_extent_after_loop_probe has40 certificates/34 replayed,
and qualified_constants_body has4/3. Assertions remain unchanged.

Next concrete input prerequisite is qualified loop-range bounds: the minimal
`record-loop-named-range.elisa` checks2/3 obligations, whereas replacing only
`0..<Limits::CAP` with literal `0..<4` checks3/3. Keep the real source spelling
and reconstruct the source constant/loop bound rather than rewriting engine
loops. Evidence files are under `build/validation/`.

## Qualified constants inside captured value loops

Prover `65dcf0fa` traverses qualified constant mentions and rewrites inside
original value-block statements/yields, including returned and initializer
loops. Namespace shadow scans now inspect nested expressions and loop/branch
headers; ambiguous or shadowed names refuse. Original engine sources and
contracts remain unchanged.

Strict O2 pair `8483683a13bb4c07860f18ec80dbb21e` passes seven controls
on both routes: returned, initializer and private loops; oversized range,
parameter shadow and nested returned/initializer body shadows refuse. The
record-array controls and kernel inventory also pass. The existing
qualified_constants_body replay gap remains, so its full script is still red.
No assertion was weakened to accommodate it.

ActionInput now proves102/180 obligations (all102 replayed), with82 findings
down from100. Changes in obligation count follow normalization of source
constants; incomplete obligations remain visible. Sound assets stays153/157.
Fresh uncached engine sweep remains69/73 with the same four failed files.
Evidence: `build/validation/prover-qualified-block-final-build.log`,
`input-qualified-block.json` and `proof-qualified-block-sweep.log`.

A minimal readonly record guard using `index >= 4 or not items[index].live`
already proves3/3, retained as `record_short_circuit_index_control.elisa`.
Thus the remaining ActionInput guard bounds must be isolated with mutable
borrow/place qualifications rather than assuming short-circuit traversal is
unimplemented. Opaque math-call summaries also remain a separate prerequisite.

### Integer bounds in float-bearing functions — 2026-10-07

The producer previously disabled numerical integer reasoning for an entire function
when any parameter contained a float, including an unrelated field in a record.
It now admits integer Boolean goals only with exact integer type witnesses and
filters premises to integer comparisons and integer type/scalar markers before
using existing machine-range checks. IEEE comparisons retain their syntactic route.

Strict O2 product pair `b562831ce5044114a0e31d0b703d6e76` was built with the installed
`50b16e68` compiler/runtime and its archived source. Five index controls pass both
whole-file and function routes: integer-only, float-before and float-after are proved;
unsafe-upper and missing-guard are refused. All positive certificates replay.
`test_record_fixed_arrays.py` (eight controls), `test_qualified_block_ranges.py`
(seven controls), and the kernel inventory (10 tables, 194 entries) also pass.
ActionInput improves from 102/180 to **113/180**, with 113 replayed certificates,
zero replay gaps, and 71 findings. The full uncached sweep remains **69/73**;
the same ActionInput context/deadzone, AudioAnimEvents and SoundAssets checks fail.
Logs: `build/validation/proof-float-integer-build.log`,
`input-float-integer.json`, and `proof-float-integer-sweep.log`.

The source-level float proposition acceptance/rejection cases pass. The later
`test_float_literal_forgery.py` fails because portable replay accepts
`not (deadzone < 0.0) => deadzone >= 0.0`, which is false for NaN.
The previous pair `8483683a13bb4c07860f18ec80dbb21e` reproduces the same failure
(`build/validation/float-forgery-baseline.log`). This is an existing kernel defect
and a required next repair; this evidence does not qualify the full prover matrix.

### Portable IEEE ordering repair — 2026-10-07

The pre-existing forged negated-order acceptance is repaired in both replay paths:
comparison-atom matching and exact negative facts. Ordered complements now require
integer operands. A source-neutral recursive classifier recognizes typed integer
literals, witnessed names/fields, pinned integer constants, integer arithmetic,
integer conditional branches and witnessed unsigned elements. This preserves the
engine's existing integer proof coverage; it does not interpret float atoms numerically.

During the repair, a concurrent compiler install changed the build inputs; the
provenance guard refused publication. The final pair was rebuilt with installed
`75568f889c2e556afaa8e4810bedf5c57f523245` and matching compiler source/runtime:
generation `0b506c3368d04f3ea61ad1e13ab96563`.
`test.d/13-float-propositions.sh` now passes, including `test_float_literal_forgery.py`
with all four direct order complements and nested Boolean projection variants;
valid exact negated atoms and literal-atom controls replay. Mixed float/integer
bounds controls and the 10-table/194-entry kernel inventory pass.
The final uncached engine sweep remains **69/73**, with the same four original
failures. The first narrow integer classifier produced nine extra replay failures;
the final recursive classifier restores all nine without weakening any test.
Logs: `build/validation/proof-float-negation-build-3.log`,
`float-negation-final-controls.log`, `float-forgery-final.log`, and
`proof-float-negation-final-sweep.log`. Full matrix and shared/native qualification
with the new installed compiler remain open.

### Direct scalar witness priority — 2026-10-07

Aggregate traversal could exhaust the existing witness budgets before reaching a
record's trailing scalar count. A minimized record with eight fixed arrays and a
`used: usize` field failed a valid guarded index despite `requires used <= 4`.
The collector now visits direct scalar fields first, then aggregates, retaining
both traversal and per-function limits. No integer capacity invariant is invented.

Pair `87a538351562444ebe0fdb2adace5a53` uses compiler `75568f88` and the unchanged
replay product. `test_scalar_field_priority.py` passes five controls on both routes:
scalar-first and scalar-last are fully proved/replayed; a count bound of five,
missing capacity condition and missing index guard are refused. Mixed float bounds,
record fixed-array, qualified block-range, IEEE forgery and kernel inventory controls
pass. The uncached engine sweep remains **69/73**, with the same four failures.

ActionInput's analyzed inventory changes from 180 to **216** obligations, with
**129** replayed certificates and zero replay gaps. This is not a directly comparable
pass ratio: eight previously closed `refresh_action` goals no longer close after
analysis reaches additional opaque calls/state invalidation. The earlier
`refresh_action` fact-budget stop is gone, and more `rebind_checked` indexing is
analyzed. Private `binding_count` capacity invariants, opaque math/resource calls,
mutable loop facts and the newly exposed late bounds remain required work.
Logs: `build/validation/proof-scalar-priority-build.log`,
`input-scalar-priority.json`, and `proof-scalar-priority-sweep.log`.
