# Owned local record fixed-array admission

The bounded fixed-place collector now seeds built-in indexing and exact outer
counts for owned, reference-free local records without borrowed views. It uses
the existing depth-three/64-place collector; scalar-witness budgets stay unchanged.
Nested shapes can repeat one place, so count always takes its first (outer)
dimension, avoiding contradictory count facts. The reference/view audit accepts
record array lengths resolved by the existing unique qualified-constant importer;
unknown or ambiguous extents stay conservative.

A call-initialized local record's guarded write previously replayed 3/4 obligations;
it now replays **4/4**, zero gaps. Nine controls pass both whole-file and function
routes: literal, qualified and nested storage accepted; wrong outer bound, missing
guard, empty storage, ambiguous constants and a borrowed-view record refused.
Existing record-array, late-index and scalar-float controls pass. ActionInput
remains **199/228**, zero gaps; the full uncached sweep remains **69/73**.

Strict O2 paired generation: 3d6ac94692834732bd21b10c3d47837a, using the isolated
exact 8006b660 snapshot described in prover-main-integration-20261007.md.
A prior build refused changed inputs; it was terminal before rebuilding.
Logs in build/validation: proof-local-record-qualified-build-2.log,
local-record-fixed-controls.log, local-record-qualified-sweep.log, and
input-local-record-qualified.json.

Real audio dispatch still needs extent facts to survive captured record mutation.
An isolated direct-extent/local-slot probe changes its refusal from unsupported
indexing to an upper-bound obligation: batch.events.count's equality disappears
from the loop state. Direct constructor locals also need a separate substitution
repair (zeroed fields currently become zeroed.count). Neither engine source was
changed to hide those failures. Probes: audio-local-record-dispatch.elisa/JSON
and local-record-index-fixed.json in build/validation. Full matrix remains open.

## Captured mutation retention

The paired product generation `33cfc686b86e4bc79273392cfb97d74f` retains explicitly traced fixed-array count equalities rooted in a live record binding across captured record mutation. Ordinary runtime count facts still require type-bound provenance.

`python3.14 ../elisa-engine-proof/scripts/test_local_record_fixed_index.py` passes 13 cases through both whole-file and function routes. New controls accept counter mutation before an in-range element write, reject an oversized loop, reject a smaller shadowed record under the outer bound, and accept its own checked bound. The mutation reproducer improves from 3/4 to 4/4 with all certificates replayed and no gaps.

This does not establish AudioAnimEvents completion: the isolated dispatch probe remains 38/42 with four findings, including its index upper bound. Both record-field and copied-slot guard probes still refuse that obligation. No audio production source was changed. Build log: `build/validation/proof-fixed-extent-retention-build.log`; ignored probe reports: `build/validation/local-record-{loop,shadow}-retention-after.json` and `build/validation/audio-local-record-slot-guard.json`.

### Remaining count witness gap

A minimized `examples/local_record_fixed_count_budget.elisa` in the prover repository reproduces the audio upper-bound refusal with an unrelated record containing sixteen scalar fields. The eight-field control proves 4/4; sixteen, twenty-four and thirty-two fields prove 3/4, all without semantic errors. The refused goal retains the slot unsigned marker, guard, builtin index witness and exact fixed count equality; it lacks the array count primitive/unsigned witnesses present in the passing control. This isolates scalar witness budget exhaustion, rather than lost extent retention. Next repair: seed fixed-array count witnesses through the bounded fixed-place collector and authenticate the same source shape during replay, preserving existing scalar budgets. Ignored reports: `build/validation/record-extent-noise-{8,16,24,32}.json`.
